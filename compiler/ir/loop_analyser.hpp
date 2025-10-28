#ifndef COMPILER_IR_LOOP_ANALYZER_HPP
#define COMPILER_IR_LOOP_ANALYZER_HPP

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "basic_block.hpp"
#include "doms.hpp"
#include "graph.hpp"

namespace Compiler {
namespace IR {

struct Loop {
    BasicBlock *header = nullptr;
    std::unordered_set<BasicBlock *> blocks;
    std::vector<BasicBlock *> latches;
    Loop *parent_loop = nullptr;
    std::vector<Loop *> inner_loops;
};

struct LoopAnalyzer {
    Graph *graph;
    std::vector<std::pair<BasicBlock *, BasicBlock *>> back_edges;
    std::vector<Loop> loops;

    LoopAnalyzer() : graph(nullptr) {}  // for tests

    explicit LoopAnalyzer(Graph *g) : graph(g) {
        if (!graph || !graph->first) return;
        DominatorTree dom_tree(*g);
        collect_back_edges(dom_tree);
        populate_loops();
        build_loop_tree();
        adjust_loop_tree();  // remove bbs from inner loops + add root loop
    }

    void dump() const {
        std::cout << "Loops:\n";
        for (size_t i = 0; i < loops.size(); ++i) {
            std::cout << "Loop " << i << ":\n";
            if (loops[i].header)
                std::cout << "  Header: %" << char('A' + loops[i].header->id) << "\n";
            else
                std::cout << "  Header: null (Root Loop)\n";

            std::cout << "  Blocks: ";
            for (auto &block : loops[i].blocks)
                std::cout << "%" << char('A' + block->id) << " ";

            std::cout << "\n";
            std::cout << "  Latches: ";
            for (auto &latch : loops[i].latches)
                std::cout << "%" << char('A' + latch->id) << " ";
            std::cout << "\n";
            if (loops[i].parent_loop) {
                auto it = std::find_if(
                    loops.begin(), loops.end(),
                    [this, i](const Loop &l) { return &l == loops[i].parent_loop; });
                if (it != loops.end())
                    std::cout << "  Parent Loop: " << std::distance(loops.begin(), it)
                              << "\n";
            }
            std::cout << "  Inner Loops: ";
            for (auto &inner : loops[i].inner_loops) {
                auto it = std::find_if(loops.begin(), loops.end(),
                                       [inner](const Loop &l) { return &l == inner; });
                if (it != loops.end())
                    std::cout << std::distance(loops.begin(), it) << " ";
            }
            std::cout << "\n";
        }
    }

    bool is_equal(const LoopAnalyzer &other, bool dump_info = false) const {
        if (loops.size() != other.loops.size()) {
            if (dump_info)
                std::cerr << "different number of loops: expected: " << other.loops.size()
                          << ", got: " << loops.size() << "\n";
            return false;
        }

        std::unordered_map<BasicBlock *, const Loop *> this_loop_map;
        const Loop *this_root = nullptr;
        for (const auto &loop : loops)
            if (loop.header)
                this_loop_map[loop.header] = &loop;
            else
                this_root = &loop;

        std::unordered_map<BasicBlock *, const Loop *> other_loop_map;
        const Loop *other_root = nullptr;
        for (const auto &loop : other.loops)
            if (loop.header)
                other_loop_map[loop.header] = &loop;
            else
                other_root = &loop;

        if (this_loop_map.size() != other_loop_map.size() ||
            (this_root == nullptr) != (other_root == nullptr)) {
            if (dump_info) std::cerr << "mismatch in number of regular/root loops\n";
            return false;
        }

        // compare contents of containers as sets with dump info if needed
        auto compare_block_sets = [&](const auto &container1, const auto &container2,
                                      const char *name, BasicBlock *header) {
            std::unordered_set<BasicBlock *> set1(container1.begin(), container1.end());
            std::unordered_set<BasicBlock *> set2(container2.begin(), container2.end());
            if (set1 != set2) {
                if (dump_info)
                    std::cerr << "mismatch in content of " << name
                              << " for loop with header "
                              << (header ? std::to_string(header->id) : "ROOT") << "\n";
                return false;
            }
            return true;
        };

        for (auto const &[header, this_loop] : this_loop_map) {
            auto it = other_loop_map.find(header);
            if (it == other_loop_map.end()) {
                if (dump_info)
                    std::cerr << "Error: Loop with header " << header->id
                              << " not found in other.\n";
                return false;
            }
            const Loop *other_loop = it->second;

            if (!compare_block_sets(this_loop->blocks, other_loop->blocks, "blocks",
                                    header))
                return false;
            if (!compare_block_sets(this_loop->latches, other_loop->latches, "latches",
                                    header))
                return false;

            BasicBlock *this_parent_header =
                this_loop->parent_loop ? this_loop->parent_loop->header : nullptr;
            BasicBlock *other_parent_header =
                other_loop->parent_loop ? other_loop->parent_loop->header : nullptr;
            if (this_parent_header != other_parent_header) {
                if (dump_info)
                    std::cerr << "Error: Parent loop mismatch for loop with header "
                              << header->id << "\n";
                return false;
            }
        }

        if (this_root) {
            if (!compare_block_sets(this_root->blocks, other_root->blocks, "blocks",
                                    nullptr))
                return false;

            std::unordered_set<BasicBlock *> this_inner_headers;
            for (const auto *inner : this_root->inner_loops)
                this_inner_headers.insert(inner->header);

            std::unordered_set<BasicBlock *> other_inner_headers;
            for (const auto *inner : other_root->inner_loops)
                other_inner_headers.insert(inner->header);

            if (this_inner_headers != other_inner_headers) {
                if (dump_info) std::cerr << "Error: Root loop inner loops mismatch.\n";
                return false;
            }
        }

        return true;
    }

   private:
    void collect_back_edges(const DominatorTree &dom_tree) {
        std::vector<bool> visited(graph->basic_blocks.size(), false);
        std::vector<bool> gray_markers(graph->basic_blocks.size(), false);
        dfs_back_edges(graph->first, visited, gray_markers, dom_tree);
    }

    void dfs_back_edges(BasicBlock *u, std::vector<bool> &visited,
                        std::vector<bool> &gray_markers, const DominatorTree &dom_tree) {
        // checks if u is a latch node
        // i.e. if any of its successors is header
        // i.e. if any of its successors dominates u
        visited[u->id] = true;
        gray_markers[u->id] = true;

        // check if u->v is a back edge. if not, run dfs from v
        auto process_successor = [&](BasicBlock *v) {
            if (!v) return;
            if (gray_markers[v->id])
                back_edges.push_back({u, v});
            else if (!visited[v->id])
                dfs_back_edges(v, visited, gray_markers, dom_tree);
        };

        process_successor(u->next1);
        process_successor(u->next2);

        gray_markers[u->id] = false;
    }

    void populate_loops() {
        std::unordered_map<BasicBlock *, Loop *> header_to_loop;
        // FIXME: overkill, there may be much more latches than loops
        // but it is better than realloc with pointer loss
        loops.reserve(back_edges.size() + 1);  // +1 for root

        for (auto &edge : back_edges) {
            BasicBlock *latch = edge.first;
            BasicBlock *header = edge.second;

            Loop *loop;
            if (header_to_loop.find(header) == header_to_loop.end()) {
                // create loop if it was not met
                loops.emplace_back();
                loop = &loops.back();
                loop->header = header;
                header_to_loop[header] = loop;
            } else {
                // take existing
                loop = header_to_loop[header];
            }

            loop->latches.push_back(latch);
            loop->blocks.insert(header);
            loop->blocks.insert(latch);

            // do "dfs" going back from latch to get nodes in loop
            std::vector<BasicBlock *> node_stack;
            std::unordered_set<BasicBlock *> visited;
            node_stack.push_back(latch);
            visited.insert(latch);
            visited.insert(header);

            while (!node_stack.empty()) {
                BasicBlock *curr = node_stack.back();
                node_stack.pop_back();
                for (auto &pred : curr->preds) {
                    if (visited.find(pred) == visited.end()) {
                        visited.insert(pred);
                        node_stack.push_back(pred);
                        loop->blocks.insert(pred);
                    }
                }
            }
        }
    }

    void build_loop_tree() {
        for (size_t i = 0; i < loops.size(); ++i) {
            for (size_t j = 0; j < loops.size(); ++j) {
                if (i == j) continue;
                if (loops[j].blocks.count(loops[i].header)) {
                    // if found i's header inside j's blocks, need to link loops
                    // second check is to get immediate parent in loop tree
                    if (!loops[i].parent_loop ||
                        loops[i].parent_loop->blocks.size() > loops[j].blocks.size())
                        loops[i].parent_loop = &loops[j];
                }
            }
        }

        // set childs corresponding to already set parents
        for (size_t i = 0; i < loops.size(); ++i)
            if (loops[i].parent_loop)
                loops[i].parent_loop->inner_loops.push_back(&loops[i]);
    }

    void adjust_loop_tree() {
        // remove blocks from inner loops
        for (auto &loop : loops) {
            for (auto *inner_loop : loop.inner_loops) {
                for (auto *block_in_inner : inner_loop->blocks) {
                    loop.blocks.erase(block_in_inner);
                }
            }
        }

        // create root loop
        loops.emplace_back();
        Loop &root_loop = loops.back();
        root_loop.header = nullptr;

        std::unordered_set<BasicBlock *> blocks_in_any_loop;
        for (const auto &loop : loops)
            blocks_in_any_loop.insert(loop.blocks.begin(), loop.blocks.end());

        for (auto &bb : graph->basic_blocks)
            if (blocks_in_any_loop.find(&bb) == blocks_in_any_loop.end())
                root_loop.blocks.insert(&bb);

        for (auto &loop : loops)
            if (!loop.parent_loop && &loop != &root_loop) {
                loop.parent_loop = &root_loop;
                root_loop.inner_loops.push_back(&loop);
            }
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_LOOP_ANALYZER_HPP
