#include <cassert>
#include <iostream>

#include "basic_block.hpp"
#include "graph.hpp"
#include "instruction.hpp"
#include "linear_order.hpp"
#include "liveness_analyzer.hpp"
#include "loop_analyser.hpp"

using namespace Compiler::IR;

inline void verify_monolithic_coverage(const LiveInterval *interval, int region_start,
                                       int region_end) {
    assert(interval != nullptr && "Interval cannot be null");

    int intersection_count = 0;
    bool covers_completely = false;

    for (const auto &r : interval->ranges) {
        if (r.start < region_end && r.end > region_start) {
            intersection_count++;
            if (r.start <= region_start && r.end >= region_end) covers_completely = true;
        }
    }

    assert(intersection_count == 1 &&
           "LSRA Failure: Interval is fragmented! Multiple ranges intersect this "
           "loop/region.");
    assert(
        covers_completely &&
        "LSRA Failure: The intersecting range does not completely span the loop/region!");
}

inline bool is_live_at(const LiveInterval *interval, int pos) {
    if (!interval || interval->ranges.empty()) return false;
    for (const auto &range : interval->ranges) {
        if (pos >= range.start && pos < range.end) return true;
        if (pos < range.start) break;
    }
    return false;
}

inline bool is_in_lifetime_hole(const LiveInterval *interval, int pos) {
    if (!interval || interval->ranges.empty()) return false;

    int min_start = interval->ranges.front().start;
    int max_end = interval->ranges.front().end;
    for (const auto &range : interval->ranges) {
        if (range.start < min_start) min_start = range.start;
        if (range.end > max_end) max_end = range.end;
    }

    // it's dead, not in hole
    if (pos < min_start || pos >= max_end) return false;

    for (const auto &range : interval->ranges)
        if (pos >= range.start && pos < range.end) return false;

    return true;
}

inline void test_if_else_graph() {
    std::cout << "--- if/else liveness ---\n";
    Graph g(4);
    BasicBlock *b0 = &g.basic_blocks[0];  // if
    BasicBlock *b1 = &g.basic_blocks[1];  // true
    BasicBlock *b2 = &g.basic_blocks[2];  // false
    BasicBlock *b3 = &g.basic_blocks[3];  // join

    b0->add_next1(b1);
    b0->add_next2(b2);
    b1->add_next1(b3);
    b2->add_next1(b3);

    Instruction *v_both = b0->add_<Const64>({10});

    Instruction *v_b1_use = b0->add_<Const64>({20});
    Instruction *v_b2_use = b0->add_<Const64>({30});

    Instruction *v_b1 = b1->add_<Add64>({v_both, v_b1_use});
    Instruction *v_b2 = b2->add_<Add64>({v_both, v_b2_use});

    Instruction *v_phi = b3->add_<Phi64>({PhiInput{v_b1, b1}, PhiInput{v_b2, b2}});
    b3->add_<RetVoid>({v_phi});

    LoopAnalyzer la(&g);
    LinearOrderBuilder linear(&g, &la);
    LivenessAnalyzer liveness(linear, la);

    assert(linear.linear_order.size() == 4);
    assert(linear.linear_order[0] == b0);
    assert(linear.linear_order[3] == b3);

    std::cout << "linear tests are ok\n";

    LiveInterval *interval_b1_use = liveness.get_live_interval(v_b1_use);
    LiveInterval *interval_b2_use = liveness.get_live_interval(v_b2_use);
    LiveInterval *interval_both = liveness.get_live_interval(v_both);

    assert(interval_b1_use && interval_b2_use && interval_both);

    // depending on order, one of variables has to be in a hole of another bb
    if (b1->linear_from < b2->linear_from) {
        assert(is_in_lifetime_hole(interval_b2_use, b1->linear_from) == true);
        assert(is_in_lifetime_hole(interval_b1_use, b2->linear_from) == false);
    } else {
        assert(is_in_lifetime_hole(interval_b1_use, b2->linear_from) == true);
        assert(is_in_lifetime_hole(interval_b2_use, b1->linear_from) == false);
    }

    // used in both so no holes
    assert(is_in_lifetime_hole(interval_both, b1->linear_from) == false);
    assert(is_in_lifetime_hole(interval_both, b2->linear_from) == false);

    LiveInterval *interval_b1 = liveness.get_live_interval(v_b1);
    LiveInterval *interval_b2 = liveness.get_live_interval(v_b2);
    assert(is_live_at(interval_b1, b1->linear_to - 1) &&
           "Phi input not live at end of pred b1");
    assert(is_live_at(interval_b2, b2->linear_to - 1) &&
           "Phi input not live at end of pred b2");

    std::cout << "lifetime tests are ok\n\n";
}

inline void test_simple_loop_graph() {
    std::cout << "--- reducible loop ---\n";
    Graph g(4);
    BasicBlock *b0 = &g.basic_blocks[0];  // entry
    BasicBlock *b1 = &g.basic_blocks[1];  // header
    BasicBlock *b2 = &g.basic_blocks[2];  // body
    BasicBlock *b3 = &g.basic_blocks[3];  // exit

    b0->add_next1(b1);
    b1->add_next1(b2);
    b1->add_next2(b3);
    b2->add_next1(b1);

    Instruction *v_inv = b0->add_<Const64>({100});

    // at least sth, will not be optimized unless you call optimizer
    Instruction *v_loop = b1->add_<Const64>({1});
    Instruction *v_latch = b2->add_<Add64>({v_loop, v_loop});
    b3->add_<RetVoid>({v_inv});

    LoopAnalyzer la(&g);
    LinearOrderBuilder linear(&g, &la);
    LivenessAnalyzer liveness(linear, la);

    int b1_idx = -1, b2_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (linear.linear_order[i] == b1) b1_idx = i;
        if (linear.linear_order[i] == b2) b2_idx = i;
    }

    // check that they are together
    assert(std::abs(b1_idx - b2_idx) == 1);
    std::cout << "linear tests are ok\n";

    LiveInterval *inv_interval = liveness.get_live_interval(v_inv);

    // check that returned value is not a hole so it is not overwritten in a loop
    assert(is_in_lifetime_hole(inv_interval, b2->linear_from) == false);
    assert(is_in_lifetime_hole(inv_interval, v_latch->linear_num) == false);

    LiveInterval *loop_interval = liveness.get_live_interval(v_loop);
    // it is not used in next iteration so should not be alive
    assert(!is_live_at(loop_interval, b2->linear_to - 1) &&
           "Loop variable should be dead before backedge");

    // should be alive before its use
    assert(is_live_at(loop_interval, v_latch->linear_num - 1) &&
           "Loop variable should be live at its use in B2");

    verify_monolithic_coverage(inv_interval, b1->linear_from, b2->linear_to);

    std::cout << "liveness tests are ok\n\n";
}

inline void test_complex_nested_graph() {
    std::cout << "--- nested loops with conditionals ---\n";
    Graph g(8);
    BasicBlock *b0 = &g.basic_blocks[0];
    BasicBlock *b1 = &g.basic_blocks[1];
    BasicBlock *b2 = &g.basic_blocks[2];
    BasicBlock *b3 = &g.basic_blocks[3];
    BasicBlock *b4 = &g.basic_blocks[4];
    BasicBlock *b5 = &g.basic_blocks[5];
    BasicBlock *b6 = &g.basic_blocks[6];
    BasicBlock *b7 = &g.basic_blocks[7];

    //              /--  <- --\
    // b0 -> b1 -> b2 -> b3 -> b5 -> b6
    //        |\->b7 \-> b4 ->/      |
    //        \<-- <-----<--<--   <--/

    b0->add_next1(b1);
    b1->add_next1(b2);
    b1->add_next2(b7);
    b2->add_next1(b3);
    b2->add_next2(b4);
    b3->add_next1(b5);
    b4->add_next1(b5);
    b5->add_next1(b2);
    b5->add_next2(b6);
    b6->add_next1(b1);

    Instruction *v_outer_inv = b1->add_<Const64>({50});
    b6->add_<Add64>({v_outer_inv, v_outer_inv});

    Instruction *v_inner_def_T = b2->add_<Const64>({25});
    b3->add_<Add64>({v_inner_def_T, v_inner_def_T});

    Instruction *v_inner_def_F = b2->add_<Const64>({26});
    b4->add_<Add64>({v_inner_def_F, v_inner_def_F});

    b7->add_<RetVoid>({});

    LoopAnalyzer la(&g);
    LinearOrderBuilder linear(&g, &la);
    LivenessAnalyzer liveness(linear, la);

    int b1_idx = -1, b6_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (linear.linear_order[i] == b1) b1_idx = i;
        if (linear.linear_order[i] == b6) b6_idx = i;
    }
    assert(b1_idx != -1 && b6_idx != -1);

    // check that only insides-of-loop are inside indices of loop
    for (int i = b1_idx; i <= b6_idx; ++i) {
        BasicBlock *bb = linear.linear_order[i];
        assert(bb != b0 && bb != b7);
    }
    std::cout << "linear tests are ok\n";

    // it has to live inside loop
    LiveInterval *outer_inv_int = liveness.get_live_interval(v_outer_inv);
    assert(is_in_lifetime_hole(outer_inv_int, b5->linear_from) == false);

    LiveInterval *inner_def_T_int = liveness.get_live_interval(v_inner_def_T);
    LiveInterval *inner_def_F_int = liveness.get_live_interval(v_inner_def_F);

    // do same check as in test1
    if (b3->linear_from < b4->linear_from) {
        assert(is_in_lifetime_hole(inner_def_F_int, b3->linear_from) == true);
        assert(is_in_lifetime_hole(inner_def_T_int, b4->linear_from) == false);
    } else {
        assert(is_in_lifetime_hole(inner_def_T_int, b4->linear_from) == true);
        assert(is_in_lifetime_hole(inner_def_F_int, b3->linear_from) == false);
    }

    int inner_header_start = b2->linear_from;
    int inner_latch_end = b5->linear_to;
    verify_monolithic_coverage(outer_inv_int, inner_header_start, inner_latch_end);

    std::cout << "lifetime checks are ok\n\n";
}

inline void run_linear_lifetime_tests() {
    test_if_else_graph();
    test_simple_loop_graph();
    test_complex_nested_graph();
    std::cout << "ALL LINEAR/LIFETIME TESTS WERE PASSED SUCCESSFULLY.\n";
}
