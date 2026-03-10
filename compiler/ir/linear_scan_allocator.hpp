#ifndef COMPILER_IR_LINEAR_SCAN_ALLOCATOR_HPP
#define COMPILER_IR_LINEAR_SCAN_ALLOCATOR_HPP

#include <algorithm>
#include <vector>

#include "instruction.hpp"
#include "liveness_analyzer.hpp"

namespace Compiler {
namespace IR {

class LinearScanAllocator {
   public:
    int R;
    int next_stack_location = 0;
    std::vector<int> free_registers;
    std::vector<LiveInterval *> active;

    LinearScanAllocator(LivenessAnalyzer &liveness, int num_registers)
        : R(num_registers) {
        for (int i = num_registers - 1; i >= 0; --i) free_registers.push_back(i);

        allocate(liveness);
    }

   private:
    int get_startpoint(const LiveInterval *i) const { return i->ranges.front().start; }

    int get_endpoint(const LiveInterval *i) const { return i->ranges.back().end; }

    void allocate(LivenessAnalyzer &liveness) {
        std::vector<LiveInterval *> intervals;
        for (auto &pair : liveness.intervals)
            if (!pair.second.ranges.empty()) intervals.push_back(&pair.second);

        std::sort(intervals.begin(), intervals.end(),
                  [this](const LiveInterval *a, const LiveInterval *b) {
                      return get_startpoint(a) < get_startpoint(b);
                  });

        // LinearScanRegisterAllocation
        // active <- {}
        active.clear();

        // for each live interval i, in order of increasing start point do
        for (LiveInterval *i : intervals) {
            ExpireOldIntervals(i);

            if (active.size() == static_cast<size_t>(R)) {
                SpillAtInterval(i);
            } else {
                // register[i] <- a register removed from pool of free registers
                int reg = free_registers.back();
                free_registers.pop_back();
                i->reg->loc.type = LocationType::REGISTER;
                i->reg->loc.value = reg;

                // add i to active, sorted by increasing end point
                insert_active(i);
            }
        }
    }

    void ExpireOldIntervals(LiveInterval *i) {
        // for each interval j in active, in order of increasing end point do
        auto it = active.begin();
        while (it != active.end()) {
            LiveInterval *j = *it;
            // if endpoint[j] > startpoint[i] then
            if (get_endpoint(j) > get_startpoint(i))
                // return
                return;
            // remove j from active
            it = active.erase(it);
            // add register[j] to pool of free registers
            free_registers.push_back(j->reg->loc.value);
        }
    }

    void SpillAtInterval(LiveInterval *i) {
        // spill <- last interval in active
        LiveInterval *spill = active.back();

        // if endpoint[spill] > endpoint[i] then
        if (get_endpoint(spill) > get_endpoint(i)) {
            // register[i] <- register[spill]
            i->reg->loc.type = LocationType::REGISTER;
            i->reg->loc.value = spill->reg->loc.value;

            // location[spill] <- new stack location
            spill->reg->loc.type = LocationType::STACK;
            spill->reg->loc.value = next_stack_location++;

            // remove spill from active
            active.pop_back();
            // add i to active, sorted by increasing end point
            insert_active(i);
        } else {
            // location[i] <- new stack location
            i->reg->loc.type = LocationType::STACK;
            i->reg->loc.value = next_stack_location++;
        }
    }

    // maintains sort order
    void insert_active(LiveInterval *i) {
        auto it = std::upper_bound(active.begin(), active.end(), i,
                                   [this](const LiveInterval *a, const LiveInterval *b) {
                                       return get_endpoint(a) < get_endpoint(b);
                                   });
        active.insert(it, i);
    }
};

}  // namespace IR
}  // namespace Compiler

#endif  // COMPILER_IR_LINEAR_SCAN_ALLOCATOR_HPP
