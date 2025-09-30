#ifndef COMPILER_IR_BASICBLOCK
#define COMPILER_IR_BASICBLOCK

#include <bitset>
#include <variant>
#include <vector>

#include "instruction.hpp"
#include "types.hpp"

namespace Compiler {
namespace IR {

struct Graph;

struct BasicBlock {
    inline static std::atomic<int> counter = 0;
    const int id;

    Instruction *first_phi = nullptr;
    Instruction *first_not_phi = nullptr;
    Instruction *last = nullptr;

    BasicBlock *next1 = nullptr;
    BasicBlock *next2 = nullptr;

    std::vector<BasicBlock *> preds;

    Graph *graph;

    BasicBlock() : id(counter++) {}

    Instruction *add_instruction(opcode_t opcode, Types::Type type,
                                 std::vector<Input> inputs,
                                 std::bitset<1> flags = 0) {
        Instruction *newinst = new Instruction(last, nullptr, opcode, type,
                                               this, inputs, {}, flags);

        for (auto &i : inputs)
            if (std::holds_alternative<Instruction *>(i.data))
                std::get<Instruction *>(i.data)->users.push_back(newinst);

        if (last) last->next = newinst;
        if (!last) {
            if (opcode == PHI_OPCODE)
                first_phi = newinst;
            else
                first_not_phi = newinst;
        }
        last = newinst;
        return newinst;
    }

    template <typename InstrTraits>
    Instruction *add_(std::vector<Input> inputs) {
        return add_instruction(InstrTraits::opcode, InstrTraits::type, inputs);
    }

    void dump() {
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
        std::cout << "\n\n\n";
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_BASICBLOCK
