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

template <opcode_t opcode_, int flags_ = 0>
struct OpTrait {
    static const opcode_t opcode = opcode_;
    constexpr static const std::bitset<1> flags = flags_;
};

template <typename OpT, Types::Type type_>
struct TypedInst {
    static const opcode_t opcode = OpT::opcode;
    static const Types::Type type = type_;
    constexpr static const std::bitset<1> flags = OpT::flags;
};

using Add = OpTrait<'ADD'>;
using Sub = OpTrait<'SUB'>;
using Mul = OpTrait<'MUL'>;
using Phi = OpTrait<PHI_OPCODE>;
using Eq = OpTrait<'EQ'>;
using Ret = OpTrait<'RET'>;
using Const = OpTrait<'CNST'>;
using GetArg = OpTrait<'ARG'>;

using Add64 = TypedInst<Add, Types::INT64_T>;
using Sub64 = TypedInst<Sub, Types::INT64_T>;
using Mul64 = TypedInst<Mul, Types::INT64_T>;
using Phi64 = TypedInst<Phi, Types::INT64_T>;
using Const64 = TypedInst<Const, Types::INT64_T>;
using Arg64 = TypedInst<GetArg, Types::INT64_T>;
using EqBool = TypedInst<Eq, Types::BOOL_T>;
using RetVoid = TypedInst<Ret, Types::VOID_T>;

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
                Types::Type ty, BasicBlock *parent_bb, std::vector<Input> inputs,
                std::vector<User> users, std::bitset<1> initial_flags)
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
