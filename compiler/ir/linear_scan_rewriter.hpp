#ifndef COMPILER_IR_LINEAR_SCAN_REWRITER_HPP
#define COMPILER_IR_LINEAR_SCAN_REWRITER_HPP

#include <algorithm>
#include <vector>

#include "basic_block.hpp"
#include "graph.hpp"
#include "instruction.hpp"

namespace Compiler {
namespace IR {

const opcode_t MOVE_OPCODE = 'MOVE';

class LinearScanRewriter {
   public:
    int scratch_base;  // temporaries for spill/fill start after other registers

    LinearScanRewriter(Graph *graph, int R) : scratch_base(R) {
        rewrite_normal_instructions(graph);
        resolve_phi_nodes(graph);
        fix_conditional_branch_conditions(graph);
    }

   private:
    static void do_insert_before(Instruction *target, Instruction *new_inst,
                                 BasicBlock &bb) {
        new_inst->next = target;
        new_inst->prev = target->prev;
        if (target->prev) target->prev->next = new_inst;
        target->prev = new_inst;
        if (bb.first_not_phi == target) bb.first_not_phi = new_inst;
    }

    void rewrite_normal_instructions(Graph *graph) {
        for (BasicBlock &bb : graph->basic_blocks) {
            Instruction *inst = bb.first_not_phi;
            while (inst) inst = process_inst(inst, bb);
        }
    }

    Instruction *process_inst(Instruction *inst, BasicBlock &bb) {
        Instruction *next_inst = inst->next;

        if (inst->opcode == Spill::opcode || inst->opcode == Fill::opcode ||
            inst->opcode == MOVE_OPCODE)
            return next_inst;

        // insert fill before every use of a stack value
        int scratch_idx = 0;
        for (Input &inp : inst->inputs) {
            if (std::holds_alternative<Instruction *>(inp.data)) {
                Instruction *def = std::get<Instruction *>(inp.data);
                if (def->loc.type == LocationType::STACK) {
                    Instruction *fill_inst = new Instruction(
                        nullptr, nullptr, Fill::opcode, def->type, &bb, {def}, {}, 0);

                    do_insert_before(inst, fill_inst, bb);

                    // update user lists
                    def->users.emplace_back(fill_inst);
                    auto &def_users = def->users;
                    def_users.erase(
                        std::remove_if(def_users.begin(), def_users.end(),
                                       [&](const User &u) { return u.inst == inst; }),
                        def_users.end());
                    fill_inst->users.emplace_back(inst);

                    inp.data = fill_inst;
                    fill_inst->loc = {LocationType::REGISTER,
                                      scratch_base + scratch_idx++};
                }
            }
        }

        // insert spills
        if (inst->loc.type == LocationType::STACK && inst->type != Types::VOID_T) {
            const bool is_condition =
                (bb.next1 != nullptr && bb.next2 != nullptr && inst->next == nullptr);

            if (is_condition) {
                // branch needs the condition value in a register, dont spill it
                inst->loc = {LocationType::REGISTER, scratch_base};
            } else {
                int stack_slot = inst->loc.value;
                inst->loc = {LocationType::REGISTER, scratch_base};

                Instruction *spill_inst = new Instruction(inst, inst->next, Spill::opcode,
                                                          inst->type, &bb, {inst}, {}, 0);

                if (inst->next)
                    inst->next->prev = spill_inst;
                else
                    bb.last = spill_inst;
                inst->next = spill_inst;

                spill_inst->users = std::move(inst->users);
                inst->users.clear();
                inst->users.emplace_back(spill_inst);

                for (User &u : spill_inst->users)
                    for (Input &u_inp : u.inst->inputs)
                        if (std::holds_alternative<Instruction *>(u_inp.data) &&
                            std::get<Instruction *>(u_inp.data) == inst) {
                            u_inp.data = spill_inst;
                        } else if (std::holds_alternative<PhiInput>(u_inp.data)) {
                            PhiInput &phi_inp = std::get<PhiInput>(u_inp.data);
                            if (phi_inp.first == inst) phi_inp.first = spill_inst;
                        }
                spill_inst->loc = {LocationType::STACK, stack_slot};
            }
        }

        return next_inst;
    }

    void resolve_phi_nodes(Graph *graph) {
        for (BasicBlock &bb : graph->basic_blocks) {
            Instruction *phi = bb.first_phi;
            while (phi && phi->opcode == PHI_OPCODE) {
                Location dst_loc = phi->loc;
                for (Input &inp : phi->inputs)
                    if (std::holds_alternative<PhiInput>(inp.data))
                        process_phi_input(phi, std::get<PhiInput>(inp.data), dst_loc);
                phi = phi->next;
            }
        }
    }

    void process_phi_input(Instruction *phi, PhiInput &phi_inp, Location dst_loc) {
        Instruction *src = phi_inp.first;
        BasicBlock *pred = phi_inp.second;
        Location src_loc = src->loc;

        if (src_loc.type == LocationType::UNASSIGNED ||
            dst_loc.type == LocationType::UNASSIGNED)
            return;

        const bool is_conditional = (pred->next1 && pred->next2);
        Instruction *condition_instr = is_conditional ? pred->last : nullptr;
        const bool is_condition_resolution = is_conditional && (src == condition_instr);
        Instruction *insert_before =
            (is_conditional && !is_condition_resolution) ? condition_instr : nullptr;

        bool need_resolution = false;
        Instruction *res_inst = nullptr;

        if (src_loc.type == LocationType::STACK &&
            dst_loc.type == LocationType::REGISTER) {
            res_inst = create_instruction(pred, Fill::opcode, src->type, {src}, dst_loc,
                                          insert_before);
            need_resolution = true;
        } else if (src_loc.type == LocationType::REGISTER &&
                   dst_loc.type == LocationType::STACK) {
            res_inst = create_instruction(pred, Spill::opcode, src->type, {src}, dst_loc,
                                          insert_before);
            need_resolution = true;
        } else if (src_loc.type == LocationType::REGISTER &&
                   dst_loc.type == LocationType::REGISTER &&
                   src_loc.value != dst_loc.value) {
            res_inst = create_instruction(pred, MOVE_OPCODE, src->type, {src}, dst_loc,
                                          insert_before);
            need_resolution = true;
        } else if (src_loc.type == LocationType::STACK &&
                   dst_loc.type == LocationType::STACK &&
                   src_loc.value != dst_loc.value) {
            Location tmp = {LocationType::REGISTER, scratch_base};
            Instruction *fill_tmp = create_instruction(pred, Fill::opcode, src->type,
                                                       {src}, tmp, insert_before);
            res_inst = create_instruction(pred, Spill::opcode, src->type, {fill_tmp},
                                          dst_loc, insert_before);
            need_resolution = true;
        }

        if (need_resolution) {
            auto &src_users = src->users;
            src_users.erase(std::remove_if(src_users.begin(), src_users.end(),
                                           [&](const User &u) { return u.inst == phi; }),
                            src_users.end());
            res_inst->users.emplace_back(phi);
            phi_inp.first = res_inst;
        }
    }

    Instruction *create_instruction(BasicBlock *bb, opcode_t opcode, Types::Type type,
                                    std::vector<Input> inputs, Location loc,
                                    Instruction *insert_before = nullptr) {
        Instruction *new_inst =
            new Instruction(nullptr, nullptr, opcode, type, bb, inputs, {}, 0);
        new_inst->loc = loc;

        for (Input &inp : inputs)
            if (std::holds_alternative<Instruction *>(inp.data))
                std::get<Instruction *>(inp.data)->users.emplace_back(new_inst);

        if (insert_before) {
            do_insert_before(insert_before, new_inst, *bb);
        } else {
            // append
            Instruction *target = bb->last;
            new_inst->prev = target;
            if (target) target->next = new_inst;
            if (!bb->first_not_phi) bb->first_not_phi = new_inst;
            bb->last = new_inst;
        }
        return new_inst;
    }

    // to move stack conditions to registers
    void fix_conditional_branch_conditions(Graph *graph) {
        for (BasicBlock &bb : graph->basic_blocks) {
            if (bb.next1 && bb.next2 && bb.last &&
                bb.last->loc.type == LocationType::STACK) {
                Location tmp = {LocationType::REGISTER, scratch_base};
                Instruction *fill = new Instruction(bb.last, nullptr, Fill::opcode,
                                                    bb.last->type, &bb, {bb.last}, {}, 0);
                bb.last->next = fill;
                bb.last = fill;
                fill->loc = tmp;
            }
        }
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_LINEAR_SCAN_REWRITER_HPP
