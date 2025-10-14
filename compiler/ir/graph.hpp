#ifndef COMPILER_IR_GRAPH
#define COMPILER_IR_GRAPH

#include <iostream>
#include <vector>

#include "basic_block.hpp"
#include "types.hpp"

namespace Compiler {
namespace IR {

struct BasicBlock;

struct Graph {
    inline static int counter = 0;

    const int id;
    std::vector<Types::Type> args;
    std::vector<BasicBlock> basic_blocks;
    BasicBlock *first = nullptr;

    Graph(int bbnum = 1, std::vector<Types::Type> args_ = {})
        : id(counter++), args(args_), basic_blocks(bbnum), first(&basic_blocks[0]) {
        for (size_t i = 0; i < basic_blocks.size(); i++) basic_blocks[i].id = i;
    }

    void dump() {
        std::cout << "Method %" << id << " args' types: ";
        for (auto &i : args) std::cout << i << ' ';
        if (first) std::cout << ", uses bb %" << first->id;
        std::cout << "\n";
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_GRAPH
