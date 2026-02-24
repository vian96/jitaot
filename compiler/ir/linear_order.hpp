#ifndef COMPILER_IR_LINEAR_ORDER_HPP
#define COMPILER_IR_LINEAR_ORDER_HPP

#include <algorithm>
#include <vector>

#include "basic_block.hpp"
#include "graph.hpp"
#include "loop_analyser.hpp"

namespace Compiler {
namespace IR {

class LinearOrderBuilder {
   public:
    Graph *graph;
    LoopAnalyzer *loop_analyzer;
    std::vector<BasicBlock *> linear_order;

    LinearOrderBuilder(Graph *g, LoopAnalyzer *la) : graph(g), loop_analyzer(la) {
        build();
    }

   private:
    void build() {
        if (!graph || !graph->first) return;

        std::vector<int> loop_depth(graph->basic_blocks.size(), 0);

        auto compute_depth = [&](const Loop *l, auto &compute_depth_ref) -> int {
            if (!l) return 0;
            if (l->parent_loop)
                return compute_depth_ref(l->parent_loop, compute_depth_ref) + 1;
            return 0;  // root loop
        };

        for (const auto &loop : loop_analyzer->loops) {
            int depth = compute_depth(&loop, compute_depth);
            for (BasicBlock *b : loop.blocks) loop_depth[b->id] = depth;
        }

        std::vector<bool> visited(graph->basic_blocks.size(), false);
        std::vector<BasicBlock *> post_order;

        auto dfs = [&](BasicBlock *b, auto &dfs_ref) -> void {
            visited[b->id] = true;
            std::vector<BasicBlock *> succs;
            if (b->next1) succs.push_back(b->next1);
            if (b->next2) succs.push_back(b->next2);

            // visit loop exits (lower depth) first so after reverse, they will appear
            // after the contiguous loop blocks
            std::sort(succs.begin(), succs.end(), [&](BasicBlock *x, BasicBlock *y) {
                if (loop_depth[x->id] != loop_depth[y->id])
                    return loop_depth[x->id] < loop_depth[y->id];
                return x->id > y->id;  // for determinism
            });

            for (BasicBlock *succ : succs)
                if (!visited[succ->id]) dfs_ref(succ, dfs_ref);
            post_order.push_back(b);
        };

        dfs(graph->first, dfs);

        std::reverse(post_order.begin(), post_order.end());
        linear_order = std::move(post_order);

        int next_num = 0;
        for (BasicBlock *b : linear_order) {
            b->linear_from = next_num;
            next_num += 2;

            Instruction *inst = b->first_phi ? b->first_phi : b->first_not_phi;
            while (inst) {
                if (inst->opcode == PHI_OPCODE) {
                    inst->linear_num = b->linear_from;
                } else {
                    inst->linear_num = next_num;
                    next_num += 2;
                }
                inst = inst->next;
            }
            b->linear_to = next_num;
        }
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_LINEAR_ORDER_HPP
