#ifndef COMPILER_IR_CHECK_ELIMINATION_HPP
#define COMPILER_IR_CHECK_ELIMINATION_HPP

#include <unordered_set>
#include <vector>

#include "doms.hpp"
#include "graph.hpp"

namespace Compiler {
namespace IR {

inline bool is_check(Instruction *inst) { return inst->flags.test(IS_CHECK_FLAG); }

inline void optimize_dominated_checks(Graph *graph) {
    if (!graph || !graph->first) return;

    compute_immediate_dominators(graph);
    auto rpo = get_reverse_post_order(graph);

    // add in-bb index so that instrs from one bb can be compared easily
    // TODO: save that at init
    for (auto *bb : rpo) {
        int num = 0;
        for (auto *inst = bb->first_phi ? bb->first_phi : bb->first_not_phi; inst;
             inst = inst->next) {
            inst->linear_num = num++;
        }
    }

    std::unordered_set<Instruction *> to_remove;

    for (auto *bb : rpo) {
        for (auto *inst = bb->first_phi ? bb->first_phi : bb->first_not_phi; inst;
             inst = inst->next) {
            if (!is_check(inst)) continue;
            if (to_remove.count(inst)) continue;

            std::vector<User> *users = nullptr;

            assert(inst->inputs.size() >= 1);
            auto inp = inst->inputs[0];
            if (std::holds_alternative<Instruction *>(inp.data))
                users = &std::get<Instruction *>(inp.data)->users;
            else if (std::holds_alternative<PhiInput>(inp.data))
                users = &std::get<PhiInput>(inp.data).first->users;
            assert(!std::holds_alternative<int>(inp.data));
            assert(users);

            for (auto &user : *users) {
                Instruction *u_inst = user.inst;
                if (u_inst == inst) continue;
                if (to_remove.count(u_inst)) continue;

                if (is_check(u_inst) && *u_inst == *inst) {
                    bool dom = false;
                    if (inst->bb == u_inst->bb)
                        dom = inst->linear_num < u_inst->linear_num;
                    else
                        dom = dominates(inst->bb, u_inst->bb);
                    if (dom) to_remove.insert(u_inst);
                }
            }
        }
    }

    for (auto *inst : to_remove) {
        inst->bb->remove_instruction(inst);
        delete inst;
    }
}

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_CHECK_ELIMINATION_HPP
