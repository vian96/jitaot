#include <cassert>
#include <iostream>

#include "basic_block.hpp"
#include "graph.hpp"
#include "instruction.hpp"
#include "types.hpp"

#include "inliner.hpp"

using namespace Compiler::IR;

inline int test_inliner() {
    Graph callee_graph(4);
    callee_graph.args = {Types::INT64_T, Types::INT64_T};
    auto &bb2 = callee_graph.basic_blocks[0];  // block 2 start
    auto &bb3 = callee_graph.basic_blocks[1];  // block 3
    auto &bb4 = callee_graph.basic_blocks[2];  // block 4
    auto &bb5 = callee_graph.basic_blocks[3];  // block 5

    callee_graph.first = &bb2;

    // block 2 start
    auto p1 = bb2.add_<Arg64>({0});  // parameter1 -> v13
    auto p2 = bb2.add_<Arg64>({1});  // parameter2 -> v14
    auto c1_callee =
        bb2.add_<Const64>({1});  // constant 1 -> v13 (using different logical names)
    auto c10_callee = bb2.add_<Const64>({10});  // const 10 -> v24
    bb2.add_next1(&bb3);

    // block 3
    auto u2 = bb3.add_<Add64>({p1, c1_callee});   // use2 v11, v19
    auto u3 = bb3.add_<Sub64>({p2, c10_callee});  // use3 v12, v20
    bb3.add_next1(&bb4);
    bb3.add_next2(&bb5);  // 2 next blocks means conditional jump

    // block 4
    auto d3 = bb4.add_<Add64>({u2, c1_callee});  // def3 -> v16
    auto r1 = bb4.add_<Ret64>({d3});             // Return v15

    // block 5
    auto d4 = bb5.add_<Add64>({u3, c10_callee});  // def4 -> v18
    auto r2 = bb5.add_<Ret64>({d4});              // Return v17

    Graph caller_graph(2);
    auto &bb0 = caller_graph.basic_blocks[0];  // block 0 start
    auto &bb1 = caller_graph.basic_blocks[1];  // block 1

    caller_graph.first = &bb0;

    // block 0 start
    auto c1_caller = bb0.add_<Const64>({1});  // const 1 -> v6
    auto c5_caller = bb0.add_<Const64>({5});  // const 5 -> v6
    bb0.add_next1(&bb1);

    // block 1
    auto d1 = bb1.add_<Add64>({c1_caller, c5_caller});  // def1 -> v5
    auto d2 = bb1.add_<Sub64>({c5_caller, c1_caller});  // def2 -> v5

    // call v3, v4 -> v6
    auto call_inst = bb1.add_<Call64>({callee_graph.id, d1, d2});

    auto use1 = bb1.add_<Add64>({call_inst, d1});  // use1 v5, v1, v2 ...

    std::cout << "Original Caller Graph:\n";
    for (auto &bb : caller_graph.basic_blocks) bb.dump();

    std::cout << "Original Callee Graph:\n";
    for (auto &bb : callee_graph.basic_blocks) bb.dump();

    Inliner inliner([&](int id) -> Graph * {
        if (id == callee_graph.id) return &callee_graph;
        return nullptr;
    });

    bool was_modified = inliner.run(&caller_graph);

    std::cout << "\n========== AFTER INLINING ==========\n";
    for (auto &bb : caller_graph.basic_blocks) bb.dump();

    assert(was_modified == true && "inliner failed to modify the graph");

    // 2 original + 1 split cont + 4 cloned = 7 blocks
    assert(caller_graph.basic_blocks.size() == 7 &&
           "incorrect number of blocks after inlining");

    BasicBlock &split_call_bb = caller_graph.basic_blocks[1];
    BasicBlock &call_cont_bb = caller_graph.basic_blocks[2];
    BasicBlock &cloned_bb2 = caller_graph.basic_blocks[3];  // entry point of callee
    BasicBlock &cloned_bb4 = caller_graph.basic_blocks[5];  // ret branch 1
    BasicBlock &cloned_bb5 = caller_graph.basic_blocks[6];  // ret branch 2

    assert(split_call_bb.next1 == &cloned_bb2 &&
           "caller split block does not point to callee entry");
    assert(split_call_bb.next2 == nullptr &&
           "caller split block should only have unconditional jump");

    assert(cloned_bb4.next1 == &call_cont_bb &&
           "cloned ret block 4 does not point to cont block");
    assert(cloned_bb5.next1 == &call_cont_bb &&
           "cloned ret block 5 does not point to cont block");

    Instruction *phi_inst = call_cont_bb.first_phi;
    assert(phi_inst != nullptr && "continuation block is missing the return phi node");
    assert(phi_inst->opcode == PHI_OPCODE && "first phi node doesn't have phi opcode");
    assert(phi_inst->inputs.size() == 2 && "phi node should combine 2 return values");

    bool has_bb4_def = false, has_bb5_def = false;
    for (auto &inp : phi_inst->inputs) {
        auto phi_inp = std::get<PhiInput>(inp.data);
        if (phi_inp.second == &cloned_bb4) has_bb4_def = true;
        if (phi_inp.second == &cloned_bb5) has_bb5_def = true;
    }
    assert(has_bb4_def && has_bb5_def &&
           "PHI node is missing predecessor mappings from the returns");

    Instruction *first_not_phi = call_cont_bb.first_not_phi;
    assert(first_not_phi != nullptr && "Continuation block is missing use1");

    bool uses_phi = false;
    for (auto &inp : first_not_phi->inputs)
        if (std::holds_alternative<Instruction *>(inp.data))
            if (std::get<Instruction *>(inp.data) == phi_inst) uses_phi = true;
    assert(uses_phi && "use1 did not get its inputs updated to the new return PHI node");

    bool call_exists = false;
    Instruction *inst = split_call_bb.first_not_phi;
    while (inst) {
        if (inst->opcode == Call::opcode) call_exists = true;
        inst = inst->next;
    }
    assert(!call_exists && "CALL instruction was not removed from the graph");

    std::cout << "\n[SUCCESS] inlining is good!\n";
    return 0;
}
