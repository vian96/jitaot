#ifndef COMPILER_IR_GRAPH
#define COMPILER_IR_GRAPH

namespace Compiler {
namespace IR {

struct BasicBlock;

struct Graph {
    BasicBlock *first;
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_GRAPH
