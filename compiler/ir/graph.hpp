#ifndef COMPILER_IR_GRAPH
#define COMPILER_IR_GRAPH

#include <vector>
#include <iostream>
#include "types.hpp"
#include "basic_block.hpp"

namespace Compiler {
namespace IR {

struct BasicBlock;

struct Graph {
    inline static int counter = 0;

    const int id;
    BasicBlock *first;
    std::vector<Types::Type> args;

    Graph(BasicBlock *first_, std::vector<Types::Type> args_)
        : id(counter++), first(first_), args(args_) {}

    void dump() {
        std::cout << "Method %" << id << " args' types: ";
        for (auto &i:args) std::cout << i << ' ';
        if (first)
            std::cout << ", uses bb %" << first->id;
        std::cout << "\n";
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_GRAPH
