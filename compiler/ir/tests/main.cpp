#include "basic_block.hpp"
#include "instruction.hpp"
#include "graph.hpp"
#include "types.hpp"

using namespace Compiler::IR;

int main() {
    // Construct factorial function (input > 1)
    // based on working LLVM IR in fact.ll
    BasicBlock entry, loop, ret;
    [[maybe_unused]] Graph graph{&entry, {Types::INT64_T}};

    auto n = entry.add_<GetArg>({Input(0)});
    auto int1 = entry.add_<Const>({Input(1)});
    entry.next1 = &loop;

    loop.preds.push_back(&entry);

    auto iphi = loop.add_<Phi>({}); // added later
    auto accphi = loop.add_<Phi>({}); // added later

    Input inp_i{iphi}, inp_acc{accphi}, inp_1{1};
    auto dec = loop.add_<Sub>({inp_i, inp_1});
    auto mul = loop.add_<Mul>({inp_acc, inp_i});
    [[maybe_unused]] auto cmp = loop.add_<Eq>({Input(dec), inp_1});

    iphi->add_input(PhiInput(n, &entry));
    iphi->add_input(PhiInput(dec, &loop));

    accphi->add_input(PhiInput(int1, &entry));
    accphi->add_input(PhiInput(mul, &loop));

    loop.next1 = &ret;
    loop.next2 = &loop;
    loop.preds.push_back(&loop);
    ret.preds.push_back(&loop);
    ret.add_<Ret>({mul});

    graph.dump();
    entry.dump();
    loop.dump();
    ret.dump();
}
