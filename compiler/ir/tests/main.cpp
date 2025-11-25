#include <unordered_map>

#include "basic_block.hpp"
#include "doms.hpp"
#include "graph.hpp"
#include "instruction.hpp"
#include "loop_analyser.hpp"
#include "optimizer.hpp"
#include "types.hpp"

using namespace Compiler::IR;

void test_construct(bool print = true) {
    // Construct factorial function (input > 1)
    // based on working LLVM IR in fact.ll
    Graph graph{3, {Types::INT64_T}};
    BasicBlock &entry = graph.basic_blocks[0], &loop = graph.basic_blocks[1],
               &ret = graph.basic_blocks[2];

    auto n = entry.add_<Arg64>({Input(0)});
    auto int1 = entry.add_<Const64>({Input(1)});
    entry.next1 = &loop;

    loop.preds.push_back(&entry);

    auto iphi = loop.add_<Phi64>({});    // added later
    auto accphi = loop.add_<Phi64>({});  // added later

    Input inp_i{iphi}, inp_acc{accphi}, inp_1{1};

    auto dec = loop.add_<Sub64>({inp_i, inp_1});
    auto mul = loop.add_<Mul64>({inp_acc, inp_i});
    [[maybe_unused]] auto cmp = loop.add_<EqBool>({Input(dec), inp_1});

    iphi->add_input(PhiInput(n, &entry));
    iphi->add_input(PhiInput(dec, &loop));

    accphi->add_input(PhiInput(int1, &entry));
    accphi->add_input(PhiInput(mul, &loop));

    loop.next1 = &ret;
    loop.next2 = &loop;
    loop.preds.push_back(&loop);
    ret.preds.push_back(&loop);
    ret.add_<RetVoid>({mul});

    compute_immediate_dominators(&graph);  // just to check that it somehow works

    if (print) {
        graph.dump();
        entry.dump();
        loop.dump();
        ret.dump();
    }
}

bool test_dom_tree1() {
    // A->B, B->C, B->F, C->D, F->E, F->G, E->D, G->D
    Graph graph{7};  // A..G
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5],
               &G = graph.basic_blocks[6];
    A.add_next1(&B);
    B.add_next1(&C);
    B.add_next2(&F);
    C.add_next1(&D);
    F.add_next1(&E);
    F.add_next2(&G);
    E.add_next1(&D);
    G.add_next1(&D);

    DominatorTree actual_tree(graph);

    // Expected: idom(A)=A, idom(B)=A, idom(C)=B, idom(F)=B, idom(E)=F, idom(D)=B,
    // idom(G)=F
    DominatorTree expected_tree;
    expected_tree.nodes.resize(7);
    std::unordered_map<BasicBlock *, DomTreeNode *> node_map;
    for (size_t i = 0; i < 7; ++i) {
        expected_tree.nodes[i].block = &graph.basic_blocks[i];
        node_map[&graph.basic_blocks[i]] = &expected_tree.nodes[i];
    }
    // A (root)
    node_map[&A]->parent = nullptr;
    node_map[&A]->childs = {node_map[&B]};
    // B
    node_map[&B]->parent = node_map[&A];
    node_map[&B]->childs = {node_map[&C], node_map[&D], node_map[&F]};
    // F
    node_map[&F]->parent = node_map[&B];
    node_map[&F]->childs = {node_map[&E], node_map[&G]};
    // C, D, E, G are leaves in the dom tree
    node_map[&C]->parent = node_map[&B];
    node_map[&D]->parent = node_map[&B];
    node_map[&E]->parent = node_map[&F];
    node_map[&G]->parent = node_map[&F];

    return actual_tree.is_equal(expected_tree);
}

bool test_dom_tree2() {
    Graph graph{11};  // A..K
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5],
               &G = graph.basic_blocks[6], &H = graph.basic_blocks[7],
               &I = graph.basic_blocks[8], &J = graph.basic_blocks[9],
               &K = graph.basic_blocks[10];
    // A->B->C->D->E->F->G->H
    // H->B, B->J->C, D->C, F->E, G->I, I->K
    A.add_next1(&B);
    B.add_next1(&C);
    B.add_next2(&J);
    C.add_next1(&D);
    D.add_next1(&E);
    D.add_next2(&C);
    E.add_next1(&F);
    F.add_next1(&G);
    F.add_next2(&E);
    G.add_next1(&H);
    G.add_next2(&I);
    H.add_next1(&B);
    I.add_next1(&K);
    J.add_next1(&C);

    DominatorTree actual_tree(graph);

    // Expected: idom(A)=A, idom(B)=A, idom(J)=B, idom(C)=B, idom(D)=C, idom(E)=D,
    //           idom(F)=E, idom(G)=F, idom(H)=G, idom(I)=G, idom(K)=I
    DominatorTree expected_tree;
    expected_tree.nodes.resize(11);
    std::unordered_map<BasicBlock *, DomTreeNode *> node_map;
    for (size_t i = 0; i < 11; ++i) {
        expected_tree.nodes[i].block = &graph.basic_blocks[i];
        node_map[&graph.basic_blocks[i]] = &expected_tree.nodes[i];
    }
    node_map[&A]->childs = {node_map[&B]};
    node_map[&B]->parent = node_map[&A];

    node_map[&B]->childs = {node_map[&C], node_map[&J]};
    node_map[&J]->parent = node_map[&B];
    node_map[&C]->parent = node_map[&B];

    node_map[&C]->childs = {node_map[&D]};
    node_map[&D]->parent = node_map[&C];

    node_map[&D]->childs = {node_map[&E]};
    node_map[&E]->parent = node_map[&D];

    node_map[&E]->childs = {node_map[&F]};
    node_map[&F]->parent = node_map[&E];

    node_map[&F]->childs = {node_map[&G]};
    node_map[&G]->parent = node_map[&F];

    node_map[&G]->childs = {node_map[&H], node_map[&I]};
    node_map[&H]->parent = node_map[&G];

    node_map[&I]->parent = node_map[&G];
    node_map[&I]->childs = {node_map[&K]};
    node_map[&K]->parent = node_map[&I];

    return actual_tree.is_equal(expected_tree);
}

bool test_dom_tree3() {
    Graph graph{9};  // A..I
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5],
               &G = graph.basic_blocks[6], &H = graph.basic_blocks[7],
               &I = graph.basic_blocks[8];
    // a->b->c->d->g->i
    //     ->e->d   ->c
    //        ->f->h->g
    //           ->b
    //             h->i
    A.add_next1(&B);
    B.add_next1(&E);
    B.add_next2(&C);
    C.add_next1(&D);
    D.add_next1(&G);
    E.add_next1(&F);
    E.add_next2(&D);
    F.add_next1(&H);
    G.add_next1(&I);
    G.add_next2(&C);
    H.add_next1(&I);
    H.add_next2(&G);

    DominatorTree actual_tree(graph);

    // Expected: idom(A)=A, idom(B)=A, idom(C)=B, idom(E)=B, idom(D)=B,
    //           idom(F)=E, idom(H)=F, idom(G)=B, idom(I)=B
    DominatorTree expected_tree;
    expected_tree.nodes.resize(9);
    std::unordered_map<BasicBlock *, DomTreeNode *> node_map;
    for (size_t i = 0; i < 9; ++i) {
        expected_tree.nodes[i].block = &graph.basic_blocks[i];
        node_map[&graph.basic_blocks[i]] = &expected_tree.nodes[i];
    }
    node_map[&A]->childs = {node_map[&B]};
    node_map[&B]->parent = node_map[&A];

    node_map[&B]->childs = {node_map[&C], node_map[&D], node_map[&E],
                            node_map[&G], node_map[&G], node_map[&I]};
    node_map[&C]->parent = node_map[&B];
    node_map[&D]->parent = node_map[&B];
    node_map[&E]->parent = node_map[&B];
    node_map[&G]->parent = node_map[&B];
    node_map[&I]->parent = node_map[&B];

    node_map[&E]->childs = {node_map[&F]};
    node_map[&F]->parent = node_map[&E];

    node_map[&F]->childs = {node_map[&H]};
    node_map[&H]->parent = node_map[&F];

    return actual_tree.is_equal(expected_tree);
}

bool test_loop_analyzer1() {
    Graph graph{7};  // copy-paste fro test_domtree1
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5],
               &G = graph.basic_blocks[6];
    A.add_next1(&B);
    B.add_next1(&C);
    B.add_next2(&F);
    C.add_next1(&D);
    F.add_next1(&E);
    F.add_next2(&G);
    E.add_next1(&D);
    G.add_next1(&D);

    LoopAnalyzer la(&graph);
    LoopAnalyzer expected_la;
    // only root loop with all nodes in it
    expected_la.loops.emplace_back();
    Loop &root_loop = expected_la.loops.back();
    root_loop.header = nullptr;
    root_loop.blocks = {&A, &B, &C, &D, &E, &F, &G};

    return la.is_equal(expected_la, true);
}

bool test_loop_analyzer2() {
    Graph graph{11};  // copy-paste fro test_domtree2
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5],
               &G = graph.basic_blocks[6], &H = graph.basic_blocks[7],
               &I = graph.basic_blocks[8], &J = graph.basic_blocks[9],
               &K = graph.basic_blocks[10];

    A.add_next1(&B);
    B.add_next1(&C);
    B.add_next2(&J);
    C.add_next1(&D);
    D.add_next1(&E);
    D.add_next2(&C);
    E.add_next1(&F);
    F.add_next1(&G);
    F.add_next2(&E);
    G.add_next1(&I);
    G.add_next2(&H);
    H.add_next1(&B);
    I.add_next1(&K);
    J.add_next1(&C);

    LoopAnalyzer la(&graph);
    LoopAnalyzer expected_la;
    expected_la.loops.resize(4);

    Loop &root = expected_la.loops[0];
    Loop &l_B = expected_la.loops[1];
    Loop &l_C = expected_la.loops[2];
    Loop &l_E = expected_la.loops[3];

    root.header = nullptr;
    root.blocks = {&A, &I, &K};
    root.parent_loop = nullptr;
    root.inner_loops = {&l_B};

    l_B.header = &B;
    l_B.blocks = {&B, &G, &H, &J};
    l_B.latches = {&H};
    l_B.parent_loop = &root;
    l_B.inner_loops = {&l_C, &l_E};

    l_C.header = &C;
    l_C.blocks = {&C, &D};
    l_C.latches = {&D};
    l_C.parent_loop = &l_B;

    l_E.header = &E;
    l_E.blocks = {&E, &F};
    l_E.latches = {&F};
    l_E.parent_loop = &l_B;

    return la.is_equal(expected_la, true);
}

bool test_loop_analyzer3() {
    Graph graph{5};  // A-E
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4];
    A.add_next1(&B);
    B.add_next1(&C);
    B.add_next2(&D);
    D.add_next1(&E);
    E.add_next1(&B);

    LoopAnalyzer la(&graph);
    LoopAnalyzer expected_la;
    expected_la.loops.resize(2);
    Loop &root = expected_la.loops[0];
    Loop &l_B = expected_la.loops[1];

    root.header = nullptr;
    root.blocks = {&A, &C};
    root.inner_loops = {&l_B};

    l_B.header = &B;
    l_B.blocks = {&B, &D, &E};
    l_B.latches = {&E};
    l_B.parent_loop = &root;

    return la.is_equal(expected_la, true);
}

bool test_loop_analyzer4() {
    Graph graph{6};  // A-F
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5];

    A.add_next1(&B);
    B.add_next1(&C);
    C.add_next2(&D);
    C.add_next1(&F);
    D.add_next1(&E);
    D.add_next2(&F);
    E.add_next1(&B);

    LoopAnalyzer la(&graph);
    LoopAnalyzer expected_la;
    expected_la.loops.resize(2);
    Loop &root = expected_la.loops[0];
    Loop &l_B = expected_la.loops[1];

    root.header = nullptr;
    root.blocks = {&A, &F};
    root.inner_loops = {&l_B};

    l_B.header = &B;
    l_B.blocks = {&B, &C, &D, &E};
    l_B.latches = {&E};
    l_B.parent_loop = &root;

    return la.is_equal(expected_la, true);
}

bool test_loop_analyzer5() {
    Graph graph{8};  // A-H
    BasicBlock &A = graph.basic_blocks[0], &B = graph.basic_blocks[1],
               &C = graph.basic_blocks[2], &D = graph.basic_blocks[3],
               &E = graph.basic_blocks[4], &F = graph.basic_blocks[5],
               &G = graph.basic_blocks[6], &H = graph.basic_blocks[7];

    A.add_next1(&B);
    B.add_next1(&C);
    B.add_next2(&D);
    C.add_next1(&E);
    C.add_next2(&F);
    D.add_next1(&F);
    F.add_next1(&G);
    G.add_next1(&B);
    G.add_next2(&H);
    H.add_next1(&A);

    LoopAnalyzer la(&graph);
    LoopAnalyzer expected_la;
    expected_la.loops.resize(3);
    Loop &root = expected_la.loops[0];
    Loop &l_A = expected_la.loops[1];
    Loop &l_B = expected_la.loops[2];

    root.header = nullptr;
    root.blocks = {&E};
    root.inner_loops = {&l_A};

    l_A.header = &A;
    l_A.blocks = {&A, &H};
    l_A.latches = {&H};
    l_A.parent_loop = &root;
    l_A.inner_loops = {&l_B};

    l_B.header = &B;
    l_B.blocks = {&B, &C, &D, &F, &G};
    l_B.latches = {&G};
    l_B.parent_loop = &l_A;

    return la.is_equal(expected_la, true);
}

bool test_constant_folding() {
    Graph graph{1};
    BasicBlock &bb = graph.basic_blocks[0];

    // immediate values: 10 - 3 = 7
    auto sub_imm = bb.add_<Sub64>({Input(10), Input(3)});

    // instruction constants: 20 & 12 = 4 (10100 & 01100 = 00100)
    auto c20 = bb.add_<Const64>({Input(20)});
    auto c12 = bb.add_<Const64>({Input(12)});
    auto and_inst = bb.add_<And64>({Input(c20), Input(c12)});

    // simple chained folding: (10 - 3) >> 1 => 7 >> 1 = 3
    auto shr_inst = bb.add_<Shr64>({Input(sub_imm), Input(1)});

    Optimizer::constant_folding(&graph);

    if (sub_imm->opcode != Const::opcode) return false;
    if (std::get<int>(sub_imm->inputs[0].data) != 7) return false;
    if (and_inst->opcode != Const::opcode) return false;
    if (std::get<int>(and_inst->inputs[0].data) != 4) return false;
    if (shr_inst->opcode != Const::opcode) return false;
    if (std::get<int>(shr_inst->inputs[0].data) != 3) return false;

    return true;
}

bool test_constant_folding_deep_chain() {
    Graph graph{1};
    BasicBlock &bb = graph.basic_blocks[0];

    // v0 = 100
    // v1 = v0 - 20  (80)
    // v2 = v1 >> 3  (80 / 8 = 10)
    // v3 = v2 & 7   (10 & 7 = 1010 & 0111 = 0010 = 2)
    // v4 = v3 - v2  (2 - 10 = -8)

    auto v0 = bb.add_<Const64>({Input(100)});
    auto v1 = bb.add_<Sub64>({Input(v0), Input(20)});
    auto v2 = bb.add_<Shr64>({Input(v1), Input(3)});
    auto v3 = bb.add_<And64>({Input(v2), Input(7)});
    auto v4 = bb.add_<Sub64>({Input(v3), Input(v2)});

    Optimizer::constant_folding(&graph);

    if (v4->opcode != Const::opcode) return false;
    if (std::get<int>(v4->inputs[0].data) != -8) return false;

    // intermidiate ones should also be optimized
    if (v1->opcode != Const::opcode || v2->opcode != Const::opcode ||
        v3->opcode != Const::opcode)
        return false;

    return true;
}

bool test_constant_folding_control_flow() {
    // BB0 (defines C1, C2) -> BB1 (uses C1, C2) -> BB2 (uses BB1 res)
    Graph graph{3};
    BasicBlock &bb0 = graph.basic_blocks[0];
    BasicBlock &bb1 = graph.basic_blocks[1];
    BasicBlock &bb2 = graph.basic_blocks[2];

    bb0.add_next1(&bb1);
    bb1.add_next1(&bb2);

    auto c100 = bb0.add_<Const64>({Input(100)});
    auto c50 = bb0.add_<Const64>({Input(50)});
    auto sub = bb1.add_<Sub64>({Input(c100), Input(c50)});
    auto res = bb2.add_<And64>({Input(sub), Input(32)});

    Optimizer::constant_folding(&graph);

    if (res->opcode != Const::opcode) return false;
    if (std::get<int>(res->inputs[0].data) != 32) return false;

    return true;
}

bool test_constant_folding_negative() {
    Graph graph{1};
    BasicBlock &bb = graph.basic_blocks[0];

    auto arg0 = bb.add_<Arg64>({Input(0)});
    auto c10 = bb.add_<Const64>({Input(10)});

    auto sub1 = bb.add_<Sub64>({Input(arg0), Input(c10)});
    auto sub2 = bb.add_<Sub64>({Input(c10), Input(arg0)});
    auto and1 = bb.add_<And64>({Input(arg0), Input(arg0)});

    Optimizer::constant_folding(&graph);

    if (sub1->opcode == Const::opcode) return false;
    if (sub2->opcode == Const::opcode) return false;
    if (and1->opcode == Const::opcode) return false;

    return true;
}

bool test_peepholes() {
    Graph graph{1};
    BasicBlock &bb = graph.basic_blocks[0];

    auto c0 = bb.add_<Const64>({Input(0)});
    auto c_minus_1 = bb.add_<Const64>({Input(-1)});
    auto arg0 = bb.add_<Arg64>({Input(0)});  // x: argument of function

    // sub x, 0 -> x
    auto sub_zero = bb.add_<Sub64>({Input(arg0), Input(0)});
    // sub x, x -> 0
    auto sub_self = bb.add_<Sub64>({Input(arg0), Input(arg0)});

    // and x, 0 -> 0
    auto and_zero = bb.add_<And64>({Input(arg0), Input(c0)});
    // and x, -1 -> x
    auto and_minus_1 = bb.add_<And64>({Input(arg0), Input(c_minus_1)});
    // and x, x -> x
    auto and_self = bb.add_<And64>({Input(arg0), Input(arg0)});

    // shr x, 0 -> x
    auto shr_zero = bb.add_<Shr64>({Input(arg0), Input(c0)});
    // shr x, 70 -> 0
    auto shr_huge = bb.add_<Shr64>({Input(arg0), Input(70)});

    // need users of optimizable-to-other-inst instructions to see if they were actually
    // optimized since optimizer only changes users, not the instruction itself
    auto check_sub_zero = bb.add_<Sub64>({Input(sub_zero), Input(1)});
    auto check_and_minus_1 = bb.add_<Sub64>({Input(and_minus_1), Input(1)});
    auto check_and_self = bb.add_<Sub64>({Input(and_self), Input(1)});
    auto check_shr_zero = bb.add_<Sub64>({Input(shr_zero), Input(1)});

    Optimizer::peephole_pass(&graph);

    if (!std::holds_alternative<Instruction *>(check_sub_zero->inputs[0].data) ||
        std::get<Instruction *>(check_sub_zero->inputs[0].data) != arg0)
        return false;

    if (sub_self->opcode != Const::opcode || std::get<int>(sub_self->inputs[0].data) != 0)
        return false;

    if (and_zero->opcode != Const::opcode || std::get<int>(and_zero->inputs[0].data) != 0)
        return false;

    if (!std::holds_alternative<Instruction *>(check_and_minus_1->inputs[0].data) ||
        std::get<Instruction *>(check_and_minus_1->inputs[0].data) != arg0)
        return false;

    if (!std::holds_alternative<Instruction *>(check_and_self->inputs[0].data) ||
        std::get<Instruction *>(check_and_self->inputs[0].data) != arg0)
        return false;

    if (!std::holds_alternative<Instruction *>(check_shr_zero->inputs[0].data) ||
        std::get<Instruction *>(check_shr_zero->inputs[0].data) != arg0)
        return false;

    if (shr_huge->opcode != Const::opcode || std::get<int>(shr_huge->inputs[0].data) != 0)
        return false;

    return true;
}

bool test_peephole_swapped_args() {
    Graph graph{1};
    BasicBlock &bb = graph.basic_blocks[0];

    auto arg0 = bb.add_<Arg64>({Input(0)});
    auto c0 = bb.add_<Const64>({Input(0)});
    auto c_minus_1 = bb.add_<Const64>({Input(-1)});

    auto and_zero_first = bb.add_<And64>({Input(c0), Input(arg0)});
    auto and_minus_1_first = bb.add_<And64>({Input(c_minus_1), Input(arg0)});
    auto shr_zero_first = bb.add_<Shr64>({Input(c0), Input(arg0)});

    auto check1 = bb.add_<Sub64>({Input(and_zero_first), Input(1)});
    auto check2 = bb.add_<Sub64>({Input(and_minus_1_first), Input(1)});
    auto check3 = bb.add_<Sub64>({Input(shr_zero_first), Input(1)});

    Optimizer::peephole_pass(&graph);

    if (and_zero_first->opcode != Const::opcode ||
        std::get<int>(and_zero_first->inputs[0].data) != 0) {
        std::cout << "Failed: and 0, x -> 0\n";
        return false;
    }

    if (!std::holds_alternative<Instruction *>(check2->inputs[0].data) ||
        std::get<Instruction *>(check2->inputs[0].data) != arg0) {
        std::cout << "Failed: and -1, x -> x\n";
        return false;
    }

    if (shr_zero_first->opcode != Const::opcode ||
        std::get<int>(shr_zero_first->inputs[0].data) != 0) {
        std::cout << "Failed: shr 0, x -> 0\n";
        return false;
    }

    return true;
}

bool test_peephole_and_fold() {
    Graph graph{1};
    BasicBlock &bb = graph.basic_blocks[0];

    // 10-(x-x)
    auto arg0 = bb.add_<Arg64>({Input(0)});
    auto p1 = bb.add_<Sub64>({Input(arg0), Input(arg0)});
    auto c10 = bb.add_<Const64>({Input(10)});
    auto f1 = bb.add_<Sub64>({Input(c10), Input(p1)});
    auto ret = bb.add_<RetVoid>({f1});

    Optimizer::optimize(&graph);
    graph.dump();
    bb.dump();

    if (p1->opcode != Const::opcode || std::get<int>(p1->inputs[0].data) != 0)
        return false;

    auto out = std::get<Instruction *>(ret->inputs[0].data);
    if (out->opcode != Const::opcode || std::get<int>(out->inputs[0].data) != 10)
        return false;

    return true;
}

int main() {
    test_construct(true);
    if (!test_dom_tree1()) {
        std::cout << "test domtree1 FAILED :(\n";
        return 1;
    }
    if (!test_dom_tree2()) {
        std::cout << "test domtree2 FAILED :(\n";
        return 1;
    }
    if (!test_dom_tree3()) {
        std::cout << "test domtree3 FAILED :(\n";
        return 1;
    }
    std::cout << "all domtree tests passed!\n";

    if (!test_loop_analyzer1()) {
        std::cout << "test loop_analyzer1 FAILED :(\n";
        return 1;
    }
    if (!test_loop_analyzer2()) {
        std::cout << "test loop_analyzer2 FAILED :(\n";
        return 1;
    }
    if (!test_loop_analyzer3()) {
        std::cout << "test loop_analyzer3 FAILED :(\n";
        return 1;
    }
    if (!test_loop_analyzer4()) {
        std::cout << "test loop_analyzer4 FAILED :(\n";
        return 1;
    }
    if (!test_loop_analyzer5()) {
        std::cout << "test loop_analyzer5 FAILED :(\n";
        return 1;
    }
    std::cout << "all loop_analyzer tests passed!\n";

    if (!test_constant_folding()) {
        std::cout << "test_constant_folding FAILED :(\n";
        return 1;
    }
    if (!test_constant_folding_deep_chain()) {
        std::cout << "test_constant_folding_deep_chain FAILED :(\n";
        return 1;
    }
    if (!test_constant_folding_control_flow()) {
        std::cout << "test_constant_folding_control_flow FAILED :(\n";
        return 1;
    }
    if (!test_constant_folding_negative()) {
        std::cout << "test_constant_folding_negative FAILED :(\n";
        return 1;
    }
    std::cout << "const fold tests passed!\n";

    if (!test_peepholes()) {
        std::cout << "test_peepholes FAILED :(\n";
        return 1;
    }
    if (!test_peephole_swapped_args()) {
        std::cout << "test_peephole_swapped_args FAILED :(\n";
        return 1;
    }
    if (!test_peephole_and_fold()) {
        std::cout << "test_peephole_and_fold FAILED :(\n";
        return 1;
    }
    std::cout << "peephole tests passed!\n";
}
