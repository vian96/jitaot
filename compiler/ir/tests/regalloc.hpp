#include "basic_block.hpp"
#include "graph.hpp"
#include "instruction.hpp"
#include "linear_order.hpp"
#include "linear_scan_allocator.hpp"
#include "linear_scan_rewriter.hpp"
#include "liveness_analyzer.hpp"
#include "loop_analyser.hpp"

using namespace Compiler::IR;

static void build_high_pressure_linear(Graph &g) {
    BasicBlock *bb = g.first;
    auto *c1 = bb->add_<Const64>({10});
    auto *c2 = bb->add_<Const64>({20});
    auto *c3 = bb->add_<Const64>({30});
    auto *c4 = bb->add_<Const64>({40});
    auto *c5 = bb->add_<Const64>({50});
    auto *a1 = bb->add_<Add64>({c1, c2});
    auto *a2 = bb->add_<Add64>({a1, c3});
    auto *a3 = bb->add_<Add64>({a2, c4});
    auto *a4 = bb->add_<Add64>({a3, c5});
    bb->add_<RetVoid>({});
}

static void build_if_else(Graph &g) {
    BasicBlock *entry = &g.basic_blocks[0];
    BasicBlock *thenb = &g.basic_blocks[1];
    BasicBlock *elseb = &g.basic_blocks[2];
    BasicBlock *join = &g.basic_blocks[3];

    auto added = entry->add_<Const64>({3});

    auto *cond =
        entry->add_<EqBool>({entry->add_<Const64>({0}), entry->add_<Const64>({0})});
    entry->add_next1(thenb);
    entry->add_next2(elseb);

    auto *val_then = thenb->add_<Const64>({42});
    thenb->add_next1(join);

    auto *val_else = elseb->add_<Const64>({99});
    elseb->add_next1(join);

    auto *phi = join->add_instruction(PHI_OPCODE, Types::INT64_T, {});
    phi->add_input(PhiInput{val_then, thenb});
    phi->add_input(PhiInput{val_else, elseb});

    auto sum = join->add_<Add64>({added, phi});
    join->add_<RetVoid>({sum});
}

static void build_reducible_loop_with_spill(Graph &g) {
    BasicBlock *preheader = &g.basic_blocks[0];
    BasicBlock *header = &g.basic_blocks[1];
    BasicBlock *body = &g.basic_blocks[2];
    BasicBlock *exit = &g.basic_blocks[3];

    auto *zero = preheader->add_<Const64>({0});
    auto *one = preheader->add_<Const64>({1});
    auto *limit = preheader->add_<Const64>({10});
    preheader->add_next1(header);

    auto *phi_counter = header->add_instruction(PHI_OPCODE, Types::INT64_T, {});
    phi_counter->add_input(PhiInput{zero, preheader});

    auto *cmp = header->add_<EqBool>({phi_counter, limit});
    header->add_next1(body);
    header->add_next2(exit);

    auto *tmp1 = body->add_<Mul64>({phi_counter, phi_counter});
    auto *tmp2 = body->add_<Add64>({tmp1, one});
    auto *tmp3 = body->add_<Mul64>({tmp2, tmp2});
    auto *update = body->add_<Add64>({tmp3, phi_counter});
    body->add_next1(header);

    phi_counter->add_input(PhiInput{update, body});

    exit->add_<RetVoid>({phi_counter});
}

static void verify_regalloc_semantics(const Graph &g, const char *test_name) {
    for (auto &bb : g.basic_blocks) bb.dump();

    std::cout << "verifying " << test_name << "\n";

    for (const auto &bb : g.basic_blocks) {
        for (Instruction *i = bb.first_phi; i; i = i->next)
            assert(i->loc.type != LocationType::UNASSIGNED && "UNASSIGNED location");
        for (Instruction *i = bb.first_not_phi; i; i = i->next)
            assert(i->loc.type != LocationType::UNASSIGNED && "UNASSIGNED location");
    }

    // should be no stack uses except for fills/spills
    for (const auto &bb : g.basic_blocks) {
        for (Instruction *i = bb.first_phi; i; i = i->next)
            if (i->opcode != PHI_OPCODE && i->opcode != Fill::opcode &&
                i->opcode != Spill::opcode && i->opcode != MOVE_OPCODE)
                for (const auto &inp : i->inputs)
                    if (std::holds_alternative<Instruction *>(inp.data))
                        assert(std::get<Instruction *>(inp.data)->loc.type ==
                               LocationType::REGISTER);
        for (Instruction *i = bb.first_not_phi; i; i = i->next)
            if (i->opcode != Fill::opcode && i->opcode != Spill::opcode &&
                i->opcode != MOVE_OPCODE)
                for (const auto &inp : i->inputs)
                    if (std::holds_alternative<Instruction *>(inp.data))
                        assert(std::get<Instruction *>(inp.data)->loc.type ==
                               LocationType::REGISTER);
    }

    // branches always use a register
    for (const auto &bb : g.basic_blocks)
        if (bb.next1 && bb.next2)
            assert(bb.last && bb.last->loc.type == LocationType::REGISTER);

    // graphs were designed to have at least one spill
    int spill_count = 0;
    for (const auto &bb : g.basic_blocks)
        for (Instruction *i = bb.first_not_phi; i; i = i->next)
            if (i->opcode == Spill::opcode) ++spill_count;

    assert(spill_count > 0 && "High-pressure graphs must produce spills");

    std::cout << "test was correct!\n";
}

inline void run_regalloc_unit_tests() {
    std::cout << "===  regalloc  ===\n\n";

    {
        Graph g(1);
        build_high_pressure_linear(g);
        LoopAnalyzer la(&g);
        LinearOrderBuilder lin(&g, &la);
        LivenessAnalyzer live(lin, la);
        LinearScanAllocator alloc(live, 2);
        LinearScanRewriter re(&g, 2);
        verify_regalloc_semantics(g, "high-pressure linear");
    }
    {
        Graph g(4);
        build_if_else(g);
        LoopAnalyzer la(&g);
        LinearOrderBuilder lin(&g, &la);
        LivenessAnalyzer live(lin, la);
        LinearScanAllocator alloc(live, 2);
        LinearScanRewriter re(&g, 2);
        verify_regalloc_semantics(g, "if-else");
    }
    {
        Graph g(4);
        build_reducible_loop_with_spill(g);
        LoopAnalyzer la(&g);
        LinearOrderBuilder lin(&g, &la);
        LivenessAnalyzer live(lin, la);

        LinearScanAllocator alloc(live, 2);
        LinearScanRewriter re(&g, 2);

        verify_regalloc_semantics(g, "reducible loop");
    }

    std::cout << "\nALL REGALLOC TESTS PASSED\n";
}
