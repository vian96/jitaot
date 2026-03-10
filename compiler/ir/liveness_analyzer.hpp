#ifndef COMPILER_IR_LIVENESS_ANALYZER_HPP
#define COMPILER_IR_LIVENESS_ANALYZER_HPP

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "basic_block.hpp"
#include "instruction.hpp"
#include "linear_order.hpp"
#include "loop_analyser.hpp"

namespace Compiler {
namespace IR {

struct LiveRange {
    int start;
    int end;

    bool operator<(const LiveRange &other) const { return start < other.start; }
};

struct LiveInterval {
    Instruction *reg = nullptr;
    std::vector<LiveRange> ranges;

    void add_range(int start, int end) {
        if (start >= end) return;

        LiveRange new_range{start, end};

        for (auto it = ranges.begin(); it != ranges.end();) {
            if (it->start <= new_range.end && new_range.start <= it->end) {
                // overlap
                new_range.start = std::min(new_range.start, it->start);
                new_range.end = std::max(new_range.end, it->end);
                it = ranges.erase(it);
            } else {
                ++it;
            }
        }

        auto it = std::lower_bound(ranges.begin(), ranges.end(), new_range);
        ranges.insert(it, new_range);
    }

    void set_from(int from) {
        if (ranges.empty())  // def without use
            ranges.push_back({from, from + 2});
        else
            ranges.front().start = from;
    }

    void dump() const {
        std::cout << "Interval for %" << (reg ? reg->id : -1) << ": ";
        for (const auto &r : ranges) std::cout << "[" << r.start << ", " << r.end << ") ";
        std::cout << "\n";
    }
};

class LivenessAnalyzer {
   public:
    std::unordered_map<Instruction *, LiveInterval> intervals;

    LivenessAnalyzer(Graph &g) {
        LoopAnalyzer la(&g);
        LinearOrderBuilder linear(&g, &la);
        build(linear.linear_order, la);
    }

    LivenessAnalyzer(const LinearOrderBuilder &linear_order_builder,
                     const LoopAnalyzer &loop_analyzer) {
        build(linear_order_builder.linear_order, loop_analyzer);
    }

    LiveInterval *get_live_interval(Instruction *inst) {
        auto it = intervals.find(inst);
        if (it != intervals.end()) return &it->second;
        return nullptr;
    }

    void dump() const {
        std::cout << "Liveness Intervals:\n";
        for (const auto &pair : intervals) pair.second.dump();
    }

   private:
    void build(const std::vector<BasicBlock *> &linear_order,
               const LoopAnalyzer &loop_analyzer) {
        std::unordered_map<BasicBlock *, std::unordered_set<Instruction *>> liveIn;

        std::unordered_map<BasicBlock *, int> linear_pos;
        for (int i = 0; i < (int)linear_order.size(); ++i)
            linear_pos[linear_order[i]] = i;

        for (auto it = linear_order.rbegin(); it != linear_order.rend(); ++it) {
            BasicBlock *b = *it;
            std::unordered_set<Instruction *> live;

            // live = union of successor.liveIn
            std::vector<BasicBlock *> successors;
            if (b->next1) successors.push_back(b->next1);
            if (b->next2) successors.push_back(b->next2);

            for (BasicBlock *succ : successors) {
                const auto &succ_live_in = liveIn[succ];
                live.insert(succ_live_in.begin(), succ_live_in.end());

                // for each phi function phi of successors of b do
                //    live.add(phi.inputOf(b))
                Instruction *inst = succ->first_phi;
                while (inst && inst->opcode == PHI_OPCODE) {
                    for (const auto &inp : inst->inputs)
                        if (std::holds_alternative<PhiInput>(inp.data)) {
                            const auto &phi_inp = std::get<PhiInput>(inp.data);
                            if (phi_inp.second == b)  // input from b
                                live.insert(phi_inp.first);
                        }
                    inst = inst->next;
                }
            }

            // for each opd in live do
            //    intervals[opd].addRange(b.from, b.to)
            for (Instruction *opd : live) {
                intervals[opd].reg = opd;
                intervals[opd].add_range(b->linear_from, b->linear_to);
            }

            // prepare for: for each operation op of b in reverse order do
            std::vector<Instruction *> insts;
            Instruction *curr = b->first_phi ? b->first_phi : b->first_not_phi;
            while (curr) {
                insts.push_back(curr);
                curr = curr->next;
            }

            for (auto op_it = insts.rbegin(); op_it != insts.rend(); ++op_it) {
                Instruction *op = *op_it;
                if (op->opcode == PHI_OPCODE) continue;

                // for each output operand opd of op do
                //      intervals[opd].setFrom(op.id)
                //      live.remove(opd)

                intervals[op].reg = op;
                intervals[op].set_from(op->linear_num);
                live.erase(op);

                // for each input operand opd of op do
                //      intervals[opd].addRange(b.from, op.id)
                //      live.add(opd)
                for (const auto &inp : op->inputs)
                    if (std::holds_alternative<Instruction *>(inp.data)) {
                        Instruction *opd = std::get<Instruction *>(inp.data);
                        intervals[opd].reg = opd;
                        intervals[opd].add_range(b->linear_from, op->linear_num);
                        live.insert(opd);
                    }
            }

            // for each phi function phi of b do
            //    live.remove(phi.output)
            curr = b->first_phi;
            while (curr && curr->opcode == PHI_OPCODE) {
                if (intervals.find(curr) == intervals.end()) {
                    // if phi result was not used sth bad may happen?
                    intervals[curr].reg = curr;
                    intervals[curr].add_range(b->linear_from, b->linear_from + 2);
                }
                live.erase(curr);
                curr = curr->next;
            }

            // if b is loop header then
            //    loopEnd = last block of the loop starting at b
            //    for each opd in live do
            //        intervals[opd].addRange(b.from, loopEnd.to)
            const Loop *header_loop = nullptr;
            for (const auto &loop : loop_analyzer.loops)
                if (loop.header == b) {
                    header_loop = &loop;
                    break;
                }
            if (header_loop) {
                BasicBlock *loopEnd = nullptr;
                int max_pos = -1;
                for (BasicBlock *loop_block : header_loop->blocks)
                    if (linear_pos[loop_block] > max_pos) {  // last one has max pos
                        max_pos = linear_pos[loop_block];
                        loopEnd = loop_block;
                    }
                if (loopEnd)
                    for (Instruction *opd : live)
                        intervals[opd].add_range(b->linear_from, loopEnd->linear_to);
            }

            liveIn[b] = live;
        }
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_LIVENESS_ANALYZER_HPP
