#ifndef COMPILER_IR_INLINER
#define COMPILER_IR_INLINER

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "basic_block.hpp"
#include "graph.hpp"
#include "instruction.hpp"

namespace Compiler {
namespace IR {

class Inliner {
   public:
    size_t max_callee_size = 50;
    size_t max_total_size = 1000;

    // used to get method graph by id. ideally by method table, in tests some lambda
    std::function<Graph *(int)> resolve_callee;

    Inliner(std::function<Graph *(int)> resolver) : resolve_callee(resolver) {}

    bool run(Graph *caller) {
        bool modified = false;
        bool changed_in_iteration = false;

        // this weird loop because inliner may invalidate iterator
        do {
            changed_in_iteration = false;
            std::vector<Instruction *> calls_to_inline;

            // 1. find calls
            for (auto &bb : caller->basic_blocks) {
                Instruction *curr = bb.first_phi ? bb.first_phi : bb.first_not_phi;
                while (curr) {
                    if (curr->opcode == Call::opcode && !curr->inputs.empty() &&
                        std::holds_alternative<int>(curr->inputs[0].data)) {
                        int callee_id = std::get<int>(curr->inputs[0].data);
                        Graph *callee = resolve_callee(callee_id);

                        // 2. if possible, inline
                        if (callee && can_inline(caller, callee)) {
                            inline_call(caller, callee, curr);
                            changed_in_iteration = true;
                            modified = true;
                            break;
                        }
                    }
                    curr = curr->next;
                }
                if (changed_in_iteration) break;
            }
        } while (changed_in_iteration);

        return modified;
    }

   private:
    bool can_inline(Graph *caller, Graph *callee) {
        if (get_graph_size(callee) > max_callee_size) return false;
        if (get_graph_size(caller) + get_graph_size(callee) > max_total_size)
            return false;
        return true;
    }

    size_t get_graph_size(Graph *g) {
        size_t size = 0;
        for (auto &bb : g->basic_blocks) {
            Instruction *curr = bb.first_phi ? bb.first_phi : bb.first_not_phi;
            while (curr) {
                size++;
                curr = curr->next;
            }
        }
        return size;
    }

    void inline_call(Graph *caller, Graph *callee, Instruction *call_inst) {
        BasicBlock *call_bb = call_inst->bb;

        // 1. split block with call
        caller->basic_blocks.emplace_back();
        BasicBlock *call_cont_block = &caller->basic_blocks.back();
        call_cont_block->id = caller->basic_blocks.size() - 1;
        call_cont_block->graph = caller;

        // move instructions after call_inst to call_cont_block
        Instruction *curr = call_inst->next;
        call_inst->next = nullptr;
        call_bb->last = call_inst;

        if (curr) curr->prev = nullptr;
        while (curr) {
            Instruction *next = curr->next;
            curr->bb = call_cont_block;
            curr->prev = call_cont_block->last;

            if (call_cont_block->last) call_cont_block->last->next = curr;

            if (curr->opcode == PHI_OPCODE) {
                if (!call_cont_block->first_phi) call_cont_block->first_phi = curr;
            } else {
                if (!call_cont_block->first_not_phi)
                    call_cont_block->first_not_phi = curr;
            }

            call_cont_block->last = curr;
            curr = next;
        }

        // transfer successors
        call_cont_block->next1 = call_bb->next1;
        call_cont_block->next2 = call_bb->next2;

        auto replace_pred = [&](BasicBlock *succ, BasicBlock *old_pred,
                                BasicBlock *new_pred) {
            if (!succ) return;
            for (auto &p : succ->preds)
                if (p == old_pred) p = new_pred;
            // update PHI nodes in successor that referenced old_pred
            Instruction *phi = succ->first_phi;
            while (phi && phi->opcode == PHI_OPCODE) {
                for (auto &inp : phi->inputs)
                    if (std::holds_alternative<PhiInput>(inp.data)) {
                        PhiInput pi = std::get<PhiInput>(inp.data);
                        if (pi.second == old_pred)
                            inp.data = PhiInput{pi.first, new_pred};
                    }
                phi = phi->next;
            }
        };

        replace_pred(call_cont_block->next1, call_bb, call_cont_block);
        replace_pred(call_cont_block->next2, call_bb, call_cont_block);
        call_bb->next1 = nullptr;
        call_bb->next2 = nullptr;

        remove_instruction(call_inst);

        // 2. clone callee blocks and instructions
        std::unordered_map<BasicBlock *, BasicBlock *> bb_map;
        std::unordered_map<Instruction *, Instruction *> inst_map;

        for (auto &callee_bb : callee->basic_blocks) {
            caller->basic_blocks.emplace_back();
            BasicBlock *cloned_bb = &caller->basic_blocks.back();
            cloned_bb->id = caller->basic_blocks.size() - 1;
            cloned_bb->graph = caller;
            bb_map[&callee_bb] = cloned_bb;

            Instruction *c =
                callee_bb.first_phi ? callee_bb.first_phi : callee_bb.first_not_phi;
            while (c) {
                Instruction *cloned_inst =
                    cloned_bb->add_instruction(c->opcode, c->type, {}, c->flags);
                inst_map[c] = cloned_inst;
                c = c->next;
            }
        }

        // 2.5 emplace actual input for instructions and find args and rets
        std::vector<Instruction *> cloned_args;
        std::vector<Instruction *> cloned_rets;

        for (auto &callee_bb : callee->basic_blocks) {
            Instruction *c =
                callee_bb.first_phi ? callee_bb.first_phi : callee_bb.first_not_phi;
            while (c) {
                Instruction *cloned_inst = inst_map[c];
                if (cloned_inst->opcode == GetArg::opcode)
                    cloned_args.push_back(cloned_inst);
                else if (cloned_inst->opcode == Ret::opcode ||
                         cloned_inst->opcode == RetVoid::opcode)
                    cloned_rets.push_back(cloned_inst);

                for (auto &inp : c->inputs) {
                    if (std::holds_alternative<Instruction *>(inp.data)) {
                        cloned_inst->add_input(
                            inst_map[std::get<Instruction *>(inp.data)]);
                    } else if (std::holds_alternative<PhiInput>(inp.data)) {
                        PhiInput pi = std::get<PhiInput>(inp.data);
                        cloned_inst->add_input(
                            PhiInput{inst_map[pi.first], bb_map[pi.second]});
                    } else {
                        cloned_inst->add_input(std::get<int>(inp.data));
                    }
                }
                c = c->next;
            }

            BasicBlock *cloned_bb = bb_map[&callee_bb];
            if (callee_bb.next1) cloned_bb->add_next1(bb_map[callee_bb.next1]);
            if (callee_bb.next2) cloned_bb->add_next2(bb_map[callee_bb.next2]);
        }

        // 3. update dataflow for parameters
        for (auto arg_inst : cloned_args) {
            int arg_idx = std::get<int>(arg_inst->inputs[0].data);

            // +1 because call_inst->inputs[0] is the callee id
            if (arg_idx + 1 < call_inst->inputs.size()) {
                Input caller_arg_input = call_inst->inputs[arg_idx + 1];

                // if argument of func is an immediate, store it as const for double
                // safety
                if (std::holds_alternative<int>(caller_arg_input.data)) {
                    Instruction *const_inst = call_bb->add_instruction(
                        Const::opcode, arg_inst->type, {caller_arg_input});
                    caller_arg_input = Input(const_inst);
                }

                // probably storing mapping would be more efficient but whatever
                for (auto &user : arg_inst->users) {
                    for (auto &inp : user.inst->inputs) {
                        if (std::holds_alternative<Instruction *>(inp.data) &&
                            std::get<Instruction *>(inp.data) == arg_inst) {
                            inp.data = caller_arg_input.data;
                            std::get<Instruction *>(caller_arg_input.data)
                                ->users.push_back(user.inst);
                        } else if (std::holds_alternative<PhiInput>(inp.data)) {
                            PhiInput pi = std::get<PhiInput>(inp.data);
                            if (pi.first == arg_inst) {
                                inp.data = PhiInput{
                                    std::get<Instruction *>(caller_arg_input.data),
                                    pi.second};
                                std::get<Instruction *>(caller_arg_input.data)
                                    ->users.push_back(user.inst);
                            }
                        }
                    }
                }
            }
            remove_instruction(arg_inst);
        }

        // 4. update dataflow for returns
        Instruction *return_val = nullptr;

        if (cloned_rets.size() == 1) {
            if (!cloned_rets[0]->inputs.empty()) {
                if (std::holds_alternative<Instruction *>(
                        cloned_rets[0]->inputs[0].data)) {
                    return_val = std::get<Instruction *>(cloned_rets[0]->inputs[0].data);
                } else if (std::holds_alternative<int>(cloned_rets[0]->inputs[0].data)) {
                    int val = std::get<int>(cloned_rets[0]->inputs[0].data);
                    return_val = cloned_rets[0]->bb->add_instruction(
                        Const::opcode, call_inst->type, {val});
                }
            }
        } else if (cloned_rets.size() > 1 && !call_inst->users.empty()) {
            std::vector<Input> phi_inputs;
            for (auto ret_inst : cloned_rets) {
                if (ret_inst->inputs.empty()) continue;

                Instruction *ret_val_inst = nullptr;
                if (std::holds_alternative<Instruction *>(ret_inst->inputs[0].data)) {
                    ret_val_inst = std::get<Instruction *>(ret_inst->inputs[0].data);
                } else if (std::holds_alternative<int>(ret_inst->inputs[0].data)) {
                    int val = std::get<int>(ret_inst->inputs[0].data);
                    ret_val_inst = ret_inst->bb->add_instruction(Const::opcode,
                                                                 call_inst->type, {val});
                }

                if (ret_val_inst)
                    phi_inputs.push_back(PhiInput{ret_val_inst, ret_inst->bb});
            }
            if (!phi_inputs.empty())
                return_val = prepend_phi(call_cont_block, call_inst->type, phi_inputs);
        }

        if (return_val && !call_inst->users.empty()) {
            for (auto &user : call_inst->users) {
                for (auto &inp : user.inst->inputs) {
                    if (std::holds_alternative<Instruction *>(inp.data) &&
                        std::get<Instruction *>(inp.data) == call_inst) {
                        inp.data = return_val;
                        return_val->users.push_back(user.inst);
                    } else if (std::holds_alternative<PhiInput>(inp.data)) {
                        PhiInput pi = std::get<PhiInput>(inp.data);
                        if (pi.first == call_inst) {
                            inp.data = PhiInput{return_val, pi.second};
                            return_val->users.push_back(user.inst);
                        }
                    }
                }
            }
            call_inst->users.clear();
        }

        // 5. jmp to and from function
        if (callee->first) call_bb->add_next1(bb_map[callee->first]);
        for (auto ret_inst : cloned_rets) {
            BasicBlock *ret_bb = ret_inst->bb;
            ret_bb->add_next1(call_cont_block);
            remove_instruction(ret_inst);
        }

        // 6. remove dead code if needed
        if (call_cont_block->preds.empty()) {
            auto remove_pred = [&](BasicBlock *succ, BasicBlock *pred) {
                if (!succ) return;
                succ->preds.erase(
                    std::remove(succ->preds.begin(), succ->preds.end(), pred),
                    succ->preds.end());

                Instruction *phi = succ->first_phi;
                while (phi && phi->opcode == PHI_OPCODE) {
                    phi->inputs.erase(
                        std::remove_if(
                            phi->inputs.begin(), phi->inputs.end(),
                            [&](Input &inp) {
                                return std::holds_alternative<PhiInput>(inp.data) &&
                                       std::get<PhiInput>(inp.data).second == pred;
                            }),
                        phi->inputs.end());
                    phi = phi->next;
                }
            };
            remove_pred(call_cont_block->next1, call_cont_block);
            remove_pred(call_cont_block->next2, call_cont_block);
            call_cont_block->next1 = nullptr;
            call_cont_block->next2 = nullptr;
        }
    }

    void remove_instruction(Instruction *inst) {
        BasicBlock *bb = inst->bb;
        if (inst->prev) inst->prev->next = inst->next;
        if (inst->next) inst->next->prev = inst->prev;

        if (bb->first_phi == inst)
            bb->first_phi =
                (inst->next && inst->next->opcode == PHI_OPCODE) ? inst->next : nullptr;
        if (bb->first_not_phi == inst)
            bb->first_not_phi =
                (inst->next && inst->next->opcode != PHI_OPCODE) ? inst->next : nullptr;
        if (bb->last == inst) bb->last = inst->prev;

        // remove it from inputs' users
        for (auto &inp : inst->inputs) {
            if (std::holds_alternative<Instruction *>(inp.data)) {
                Instruction *def = std::get<Instruction *>(inp.data);
                def->users.erase(std::remove_if(def->users.begin(), def->users.end(),
                                                [&](User &u) { return u.inst == inst; }),
                                 def->users.end());
            } else if (std::holds_alternative<PhiInput>(inp.data)) {
                Instruction *def = std::get<PhiInput>(inp.data).first;
                def->users.erase(std::remove_if(def->users.begin(), def->users.end(),
                                                [&](User &u) { return u.inst == inst; }),
                                 def->users.end());
            }
        }
    }

    Instruction *prepend_phi(BasicBlock *bb, Types::Type type,
                             std::vector<Input> inputs) {
        Instruction *new_phi =
            new Instruction(nullptr, nullptr, PHI_OPCODE, type, bb, inputs, {}, 0);
        for (auto &i : inputs) {
            if (std::holds_alternative<Instruction *>(i.data))
                std::get<Instruction *>(i.data)->users.push_back(new_phi);
            else if (std::holds_alternative<PhiInput>(i.data))
                std::get<PhiInput>(i.data).first->users.push_back(new_phi);
        }

        if (bb->first_phi) {
            new_phi->next = bb->first_phi;
            bb->first_phi->prev = new_phi;
            bb->first_phi = new_phi;
        } else {
            new_phi->next = bb->first_not_phi;
            if (bb->first_not_phi) bb->first_not_phi->prev = new_phi;
            bb->first_phi = new_phi;
            if (!bb->last) bb->last = new_phi;
        }
        return new_phi;
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_INLINER
