#ifndef COMPILER_IR_INSTRUCTION
#define COMPILER_IR_INSTRUCTION

#include <atomic>
#include <bitset>
#include <cstdint>
#include <iostream>
#include <variant>
#include <vector>

#include "types.hpp"

namespace Compiler {
namespace IR {

struct BasicBlock;
struct Instruction;

typedef uint32_t opcode_t;
const opcode_t PHI_OPCODE = 'PHI';

template <opcode_t opcode_, Types::Type type_, int flags_ = 0>
struct InstructionTraits {
    static const opcode_t opcode = opcode_;
    static const Types::Type type = type_;
    constexpr static const std::bitset<1> flags = flags_;
};

typedef InstructionTraits<'ADD', Types::INT64_T> Add;
typedef InstructionTraits<PHI_OPCODE, Types::INT64_T> Phi;
typedef InstructionTraits<'EQ', Types::BOOL_T> Eq;
typedef InstructionTraits<'SUB', Types::INT64_T> Sub;
typedef InstructionTraits<'MUL', Types::INT64_T> Mul;
typedef InstructionTraits<'RET', Types::VOID_T> Ret;
typedef InstructionTraits<'CNST', Types::INT64_T> Const;
typedef InstructionTraits<'ARG', Types::INT64_T> GetArg;

struct User {
    Instruction *inst;
    // other info
    User(Instruction *inst_) : inst(inst_) {}
};

typedef std::pair<Instruction *, BasicBlock *> PhiInput;

struct Input {
    std::variant<Instruction *, int, PhiInput> data;
    void dump();
    // other info
    Input(Instruction *inst_) : data(inst_) {}
    Input(int intval) : data(intval) {}
    Input(PhiInput phi_inp) : data(phi_inp) {}
};

struct Instruction {
    inline static std::atomic<int> counter = 0;
    const int id;

    Instruction *prev = nullptr;
    Instruction *next = nullptr;

    opcode_t opcode;
    Types::Type type;
    BasicBlock *bb;

    std::vector<Input> inputs;
    std::vector<User> users;

    std::bitset<1> flags = 0;  // throwable, ...

    void add_input(PhiInput inp) {
        inputs.emplace_back(inp);
        inp.first->users.push_back(this);
    }

    void add_input(Instruction *inp) {
        inputs.emplace_back(inp);
        inp->users.push_back(this);
    }

    void add_input(int inp) { inputs.emplace_back(inp); }

    Instruction(Instruction *prev_inst, Instruction *next_inst, opcode_t op,
                Types::Type ty, BasicBlock *parent_bb,
                std::vector<Input> inputs, std::vector<User> users,
                std::bitset<1> initial_flags)
        : id(counter++),
          prev(prev_inst),
          next(next_inst),
          opcode(op),
          type(ty),
          bb(parent_bb),
          inputs(inputs),
          users(users),
          flags(initial_flags) {}

    void dump() {
        std::cout << "instruction %" << id << ": ";
        std::cout << "type: " << type << " flags: " << flags << " opcode: ";
        const char *char_ptr = reinterpret_cast<const char *>(&opcode);
        for (int i = sizeof(opcode) - 1; i >= 0; --i)
            if (char_ptr[i]) std::cout << char_ptr[i];
        std::cout << ' ';
        std::cout << "inputs: ";
        for (auto &i : inputs) i.dump();
        std::cout << "users: ";
        for (auto &i : users) std::cout << " %" << i.inst->id;
        std::cout << std::endl;
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_INSTRUCTION
