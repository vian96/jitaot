#ifndef COMPILER_IR_OPTIMIZER_HPP
#define COMPILER_IR_OPTIMIZER_HPP

#include <algorithm>
#include <optional>
#include <variant>
#include <vector>

#include "doms.hpp"
#include "graph.hpp"
#include "instruction.hpp"

namespace Compiler {
namespace IR {

class Optimizer {
   public:
    static void constant_folding(Graph* graph) {
        if (!graph || !graph->first) return;

        std::vector<BasicBlock*> rpo = get_reverse_post_order(graph);

        for (BasicBlock* bb : rpo) {
            Instruction* inst = bb->first_phi ? bb->first_phi : bb->first_not_phi;
            while (inst) {
                Instruction* next = inst->next;  // so inst change does not affect next
                try_fold_instruction(inst);
                inst = next;
            }
        }
    }

    static void peephole_pass(Graph* graph) {
        if (!graph || !graph->first) return;

        // TODO: make cached?
        std::vector<BasicBlock*> rpo = get_reverse_post_order(graph);

        for (BasicBlock* bb : rpo) {
            Instruction* inst = bb->first_phi ? bb->first_phi : bb->first_not_phi;
            while (inst) {
                Instruction* next = inst->next;
                try_peephole_instruction(inst);
                inst = next;
            }
        }
    }

   private:
    static std::optional<int64_t> get_constant_value(const Input& input) {
        if (std::holds_alternative<int>(input.data)) return std::get<int>(input.data);

        if (std::holds_alternative<Instruction*>(input.data)) {
            Instruction* def = std::get<Instruction*>(input.data);
            if (def && def->opcode == Const::opcode && !def->inputs.empty() &&
                std::holds_alternative<int>(def->inputs[0].data)) {
                return std::get<int>(def->inputs[0].data);
            }
        }
        return std::nullopt;
    }

    static void try_fold_instruction(Instruction* inst) {
        if (inst->opcode == Const::opcode) return;

        if (inst->opcode != Sub::opcode && inst->opcode != And::opcode &&
            inst->opcode != Shr::opcode)
            return;

        if (inst->inputs.size() != 2) throw "ill-formed sub, shr or and: not 2 args";

        auto val1 = get_constant_value(inst->inputs[0]);
        auto val2 = get_constant_value(inst->inputs[1]);

        if (val1 && val2) {
            int64_t v1 = *val1;
            int64_t v2 = *val2;
            int64_t result = 0;

            switch (inst->opcode) {
                case Sub::opcode:
                    result = v1 - v2;
                    break;
                case And::opcode:
                    result = v1 & v2;
                    break;
                case Shr::opcode:
                    result = v1 >> v2;
                    break;
                default:
                    throw "not implemented opcode:(";
            }

            replace_instruction_with_const(inst, result);
        }
    }

    static void replace_instruction_with_const(Instruction* inst, int64_t val) {
        for (auto& inp : inst->inputs) {
            if (std::holds_alternative<Instruction*>(inp.data)) {
                Instruction* def = std::get<Instruction*>(inp.data);
                auto& users = def->users;
                users.erase(
                    std::remove_if(users.begin(), users.end(),
                                   [inst](const User& u) { return u.inst == inst; }),
                    users.end());
            }
        }

        inst->inputs.clear();
        inst->opcode = Const::opcode;
        inst->inputs.emplace_back(static_cast<int>(val));
    }

    static void replace_instruction_with_input(Instruction* inst, Input target) {
        // can't use simple logic like in replace_with_const since you can't just change
        // instruction to its user

        // make all users use target
        for (User& user : inst->users) {
            Instruction* user_inst = user.inst;
            bool replaced = false;
            for (auto& inp : user_inst->inputs) {
                if (std::holds_alternative<Instruction*>(inp.data) &&
                    std::get<Instruction*>(inp.data) == inst) {
                    inp = target;
                    replaced = true;
                }
            }

            // make instruction know about its users
            if (replaced && std::holds_alternative<Instruction*>(target.data))
                std::get<Instruction*>(target.data)->users.push_back(user_inst);
        }

        // make instruction dead: remove it from its inputs' users + make it nop
        for (auto& inp : inst->inputs) {
            if (std::holds_alternative<Instruction*>(inp.data)) {
                Instruction* def = std::get<Instruction*>(inp.data);
                auto& def_users = def->users;
                def_users.erase(
                    std::remove_if(def_users.begin(), def_users.end(),
                                   [inst](const User& u) { return u.inst == inst; }),
                    def_users.end());
            }
        }

        inst->users.clear();
        inst->inputs.clear();
        inst->opcode = Const::opcode;
        inst->inputs.emplace_back(0);
    }

    static bool inputs_are_equal(const Input& a, const Input& b) {
        // does not handle phi currently :( phi are not used anywhere here tho
        if (a.data.index() != b.data.index()) return false;
        if (std::holds_alternative<Instruction*>(a.data))
            return std::get<Instruction*>(a.data) == std::get<Instruction*>(b.data);
        if (std::holds_alternative<int>(a.data))
            return std::get<int>(a.data) == std::get<int>(b.data);
        return false;
    }

    static void try_peephole_instruction(Instruction* inst) {
        if (inst->inputs.size() != 2) return;

        if (inst->opcode == Sub::opcode) {
            auto val2 = get_constant_value(inst->inputs[1]);
            if (val2 && *val2 == 0) {
                replace_instruction_with_input(inst, inst->inputs[0]);
                return;
            }

            if (inputs_are_equal(inst->inputs[0], inst->inputs[1])) {
                replace_instruction_with_const(inst, 0);
                return;
            }
        }

        if (inst->opcode == And::opcode) {
            if (inputs_are_equal(inst->inputs[0], inst->inputs[1])) {
                replace_instruction_with_input(inst, inst->inputs[0]);
                return;
            }

            auto val1 = get_constant_value(inst->inputs[0]);
            auto val2 = get_constant_value(inst->inputs[1]);

            if ((val1 && *val1 == 0) || (val2 && *val2 == 0)) {
                replace_instruction_with_const(inst, 0);
                return;
            }

            if (val2 && *val2 == -1) {
                replace_instruction_with_input(inst, inst->inputs[0]);
                return;
            }
            if (val1 && *val1 == -1) {
                replace_instruction_with_input(inst, inst->inputs[1]);
                return;
            }
        }

        if (inst->opcode == Shr::opcode) {
            auto val2 = get_constant_value(inst->inputs[1]);

            if (val2 && *val2 == 0) {
                replace_instruction_with_input(inst, inst->inputs[0]);
                return;
            }

            if (val2 && *val2 >= 64) {
                replace_instruction_with_const(inst, 0);
                return;
            }
        }
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_OPTIMIZER_HPP
