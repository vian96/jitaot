#include <cassert>
#include <iostream>

#include "check_elimintaion.hpp"
#include "graph.hpp"
#include "instruction.hpp"

namespace Compiler {
namespace IR {

inline int count_opcodes(BasicBlock *bb, opcode_t opcode) {
    int count = 0;
    for (auto *inst = bb->first_phi ? bb->first_phi : bb->first_not_phi; inst;
         inst = inst->next)
        if (inst->opcode == opcode) count++;
    return count;
}

inline void test_in_block_elimination() {
    Graph g(1, {Types::INT64_T});
    auto &bb = g.basic_blocks[0];

    auto arg = bb.add_<Arg64>({0});
    auto cst = bb.add_<Const64>({10});

    bb.add_<ZeroCheck>({arg});
    bb.add_<NullCheck>({arg});
    bb.add_<BoundsCheck>({arg, cst});

    // duplicate checks
    bb.add_<ZeroCheck>({arg});
    bb.add_<NullCheck>({arg});
    bb.add_<BoundsCheck>({arg, cst});

    assert(count_opcodes(&bb, ZeroCheck::opcode) == 2);
    assert(count_opcodes(&bb, NullCheck::opcode) == 2);
    assert(count_opcodes(&bb, BoundsCheck::opcode) == 2);

    optimize_dominated_checks(&g);

    assert(count_opcodes(&bb, ZeroCheck::opcode) == 1);
    assert(count_opcodes(&bb, NullCheck::opcode) == 1);
    assert(count_opcodes(&bb, BoundsCheck::opcode) == 1);

    std::cout << "test 1 (in-block elimination) passed\n";
}

inline void test_different_args_not_eliminated() {
    Graph g(1, {Types::INT64_T, Types::INT64_T});
    auto &bb = g.basic_blocks[0];

    auto arg1 = bb.add_<Arg64>({0});
    auto arg2 = bb.add_<Arg64>({1});
    auto cst1 = bb.add_<Const64>({10});
    auto cst2 = bb.add_<Const64>({20});

    bb.add_<NullCheck>({arg1});
    bb.add_<NullCheck>({arg2});

    bb.add_<BoundsCheck>({arg1, cst1});
    bb.add_<BoundsCheck>({arg1, cst2});
    bb.add_<BoundsCheck>({arg1, cst1});

    optimize_dominated_checks(&g);

    assert(count_opcodes(&bb, NullCheck::opcode) == 2);
    assert(count_opcodes(&bb, BoundsCheck::opcode) == 2);

    std::cout << "test 2 (strict parameter matching) passed\n";
}

inline void test_complex_graph_dominance() {
    Graph g(5, {Types::INT64_T, Types::INT64_T});
    auto &bb0 = g.basic_blocks[0];  // entry
    auto &bb1 = g.basic_blocks[1];  // left branch
    auto &bb2 = g.basic_blocks[2];  // right branch
    auto &bb3 = g.basic_blocks[3];  // merge point
    auto &bb4 = g.basic_blocks[4];  // tail

    // bb0 -> (bb1, bb2) -> bb3 -> bb4
    bb0.add_next1(&bb1);
    bb0.add_next2(&bb2);
    bb1.add_next1(&bb3);
    bb2.add_next1(&bb3);
    bb3.add_next1(&bb4);

    auto arg1 = bb0.add_<Arg64>({0});
    auto arg2 = bb0.add_<Arg64>({1});

    // dominates everything
    bb0.add_<NullCheck>({arg1});

    // does NOT dominate bb3
    bb1.add_<NullCheck>({arg2});
    bb2.add_<ZeroCheck>({arg2});

    bb3.add_<NullCheck>({arg1});  // should be removed
    bb3.add_<NullCheck>({arg2});  // should be kept

    // both removed
    bb4.add_<NullCheck>({arg1});
    bb4.add_<NullCheck>({arg2});

    optimize_dominated_checks(&g);

    assert(count_opcodes(&bb0, NullCheck::opcode) == 1);
    assert(count_opcodes(&bb1, NullCheck::opcode) == 1);
    assert(count_opcodes(&bb2, NullCheck::opcode) == 1);

    assert(count_opcodes(&bb3, NullCheck::opcode) == 1);
    assert(count_opcodes(&bb4, NullCheck::opcode) == 0);

    std::cout << "test 3 (complex graph dominance) passed\n";
}

inline void run_check_elimination_tests() {
    test_in_block_elimination();
    test_different_args_not_eliminated();
    test_complex_graph_dominance();
    std::cout << "all check elimination tests passed successfully!\n";
}

}  // namespace IR
}  // namespace Compiler
