#include <algorithm>
#include <cassert>
#include <vector>

#include "basic_block.hpp"
#include "graph.hpp"

namespace Compiler {
namespace IR {

inline void post_order_DFS(BasicBlock *block, std::vector<BasicBlock *> &post_order,
                           std::vector<bool> &visited, int &counter) {
    visited[block->id] = true;

    if (block->next1 && !visited[block->next1->id])
        post_order_DFS(block->next1, post_order, visited, counter);
    if (block->next2 && !visited[block->next2->id])
        post_order_DFS(block->next2, post_order, visited, counter);

    block->post_order_number = counter++;
    post_order.push_back(block);
}

inline BasicBlock *find_common_idom(BasicBlock *b1, BasicBlock *b2) {
    // finds first common idom ("intersects" preds). all higher idoms will be the same
    BasicBlock *finger1 = b1;
    BasicBlock *finger2 = b2;

    // while sequences differ, go up until you find common dominator
    while (finger1 != finger2) {
        while (finger1->post_order_number < finger2->post_order_number)
            finger1 = finger1->idom;
        while (finger2->post_order_number < finger1->post_order_number)
            finger2 = finger2->idom;
    }
    return finger1;
}

inline void compute_immediate_dominators(Graph *graph) {
    // algorithm from Wikipedia. I liked it more, it is not slower than "slow algo" from
    // lecture and it works on test examples
    if (!graph || !graph->first) return;

    std::vector<BasicBlock *> post_order_blocks;
    std::vector<bool> visited(graph->basic_blocks.size(), false);
    int post_order_counter = 0;
    post_order_DFS(graph->first, post_order_blocks, visited, post_order_counter);

    std::vector<BasicBlock *> reverse_post_order = post_order_blocks;
    std::reverse(reverse_post_order.begin(), reverse_post_order.end());

    for (auto &block : graph->basic_blocks) block.idom = nullptr;
    graph->first->idom = graph->first;

    bool changed = true;
    while (changed) {
        changed = false;
        for (BasicBlock *b : reverse_post_order) {
            if (b == graph->first) continue;

            BasicBlock *new_idom = nullptr;
            // Find the first processed predecessor
            for (BasicBlock *p : b->preds) {
                if (p->idom != nullptr) {
                    new_idom = p;  // not p->idom as the node itself may be idom
                    break;
                }
            }

            // if any pred was processed, update this node's idom to intersection of all
            // of idoms of all preds
            if (new_idom)
                for (BasicBlock *p : b->preds)
                    if (p != new_idom && p->idom != nullptr)
                        new_idom = find_common_idom(p, new_idom);

            if (b->idom != new_idom) {
                b->idom = new_idom;
                changed = true;
            }
        }
    }
}

struct DomTreeNode {
    BasicBlock *block = nullptr;
    DomTreeNode *parent = nullptr;
    std::vector<DomTreeNode *> childs;
};

struct DominatorTree {
    std::vector<DomTreeNode> nodes;
    DomTreeNode *root = nullptr;

    DominatorTree() = default;

    explicit DominatorTree(Graph &graph) : nodes(graph.basic_blocks.size()) {
        if (graph.basic_blocks.empty()) return;

        compute_immediate_dominators(&graph);

        for (size_t i = 0; i < graph.basic_blocks.size(); i++) {
            auto &block = graph.basic_blocks[i];
            // assumes that graph's basic blocks' ids are method local indexed which is
            // true at least for now, but do assert to be sure
            assert((int)i == block.id);

            nodes[i].block = &block;
            if (block.idom != &block) {
                nodes[block.idom->id].childs.push_back(&nodes[i]);
                nodes[i].parent = &nodes[block.idom->id];
            } else {
                // source node has idom=itself, and it's a top node in tree so no parent
                nodes[i].parent = nullptr;
                root = &nodes[i];
            }
        }
    }

    template <bool DumpInfo = true>
    bool is_equal(const DominatorTree &other) {
        if (nodes.size() != other.nodes.size()) {
            if constexpr (DumpInfo) std::cerr << "different node size\n";
            return false;
        }
        for (size_t i = 0; i < nodes.size(); i++) {
            auto &node = nodes[i];
            auto &other_node = other.nodes[i];
            if (node.block->id != other_node.block->id) {
                if constexpr (DumpInfo)
                    std::cerr << "node %" << node.block->id << " != %"
                              << other_node.block->id << ":(\n";
                return false;
            }
            if (node.parent && other_node.parent) {
                if (node.parent->block->id != other_node.parent->block->id) {
                    if constexpr (DumpInfo)
                        std::cerr << "different parents for node %" << node.block->id
                                  << ": %" << node.parent->block->id << " != %"
                                  << other_node.parent->block->id << ":(\n";
                    return false;
                }
            } else if (node.parent != other_node.parent) {
                if constexpr (DumpInfo)
                    std::cerr << "node %" << node.block->id
                              << " has different nullptr'ity :(\n";
                return false;
            }
            // No need to check for childs as parents were set up in constructor
            // and they are guaranteed to be okay as long as parents are ok
        }
        return true;
    }
};

}  // namespace IR
}  // namespace Compiler
