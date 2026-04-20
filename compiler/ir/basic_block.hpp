#ifndef COMPILER_IR_BASICBLOCK
#define COMPILER_IR_BASICBLOCK

#include <algorithm>
#include <bitset>
#include <variant>
#include <vector>

#include "instruction.hpp"
#include "types.hpp"

namespace Compiler {
namespace IR {

struct Graph;

struct BasicBlock {
    int id;  // per method counter

    Instruction *first_phi = nullptr;
    Instruction *first_not_phi = nullptr;
    Instruction *last = nullptr;

    BasicBlock *next1 = nullptr;
    BasicBlock *next2 = nullptr;

    std::vector<BasicBlock *> preds;

    Graph *graph;

    BasicBlock *idom = nullptr;
    int post_order_number = -1;

    int linear_from = -1;
    int linear_to = -1;

    BasicBlock() {}

    void add_next1(BasicBlock *other) {
        next1 = other;
        other->preds.push_back(this);
    }
    void add_next2(BasicBlock *other) {
        next2 = other;
        other->preds.push_back(this);
    }

    Instruction *add_instruction(opcode_t opcode, Types::Type type,
                                 std::vector<Input> inputs, std::bitset<8> flags = 0) {
        Instruction *newinst =
            new Instruction(last, nullptr, opcode, type, this, inputs, {}, flags);

        for (auto &i : inputs)
            if (std::holds_alternative<Instruction *>(i.data))
                std::get<Instruction *>(i.data)->users.push_back(newinst);

        if (last) last->next = newinst;
        if (opcode == PHI_OPCODE) {
            if (!first_phi) first_phi = newinst;
        } else {
            if (!first_not_phi) first_not_phi = newinst;
        }
        last = newinst;
        return newinst;
    }

    void remove_instruction(Instruction *inst) {
        if (inst->prev) inst->prev->next = inst->next;
        if (inst->next) inst->next->prev = inst->prev;

        if (first_phi == inst)
            first_phi =
                (inst->next && inst->next->opcode == PHI_OPCODE) ? inst->next : nullptr;
        if (first_not_phi == inst) first_not_phi = inst->next;
        if (last == inst) last = inst->prev;

        // remove from inputs' users
        for (auto &inp : inst->inputs) {
            if (std::holds_alternative<Instruction *>(inp.data)) {
                auto &users = std::get<Instruction *>(inp.data)->users;
                users.erase(std::remove_if(users.begin(), users.end(),
                                           [inst](User &u) { return u.inst == inst; }),
                            users.end());
            } else if (std::holds_alternative<PhiInput>(inp.data)) {
                auto &users = std::get<PhiInput>(inp.data).first->users;
                users.erase(std::remove_if(users.begin(), users.end(),
                                           [inst](User &u) { return u.inst == inst; }),
                            users.end());
            }
        }
    }

    template <typename TypedInstr>
    Instruction *add_(std::vector<Input> inputs) {
        return add_instruction(TypedInstr::opcode, TypedInstr::type, inputs, TypedInstr::flags);
    }

    template <typename OpTrait>
    Instruction *add_(Types::Type type, std::vector<Input> inputs) {
        return add_instruction(OpTrait::opcode, type, inputs, OpTrait::flags);
    }

    void dump() const {
        std::cout << "basic block %" << id << ": \n";
        Instruction *inst = first_phi ? first_phi : first_not_phi;
        while (inst != nullptr) {
            inst->dump();
            inst = inst->next;
        }
        if (next1) std::cout << "next1: " << next1->id << ' ';
        if (next2) std::cout << "next2: " << next2->id << ' ';
        std::cout << "\npreds:";
        for (auto &i : preds) std::cout << " %" << i->id;
        if (idom)
            std::cout << "\nidom: " << idom->id;
        else
            std::cout << "\nno idom:(";
        std::cout << "\n\n\n";
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_BASICBLOCK
