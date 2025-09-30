#include "instruction.hpp"
#include "basic_block.hpp"

namespace Compiler {
namespace IR {

void Input::dump() {
    if (std::holds_alternative<Instruction *>(data))
        std::cout << '%' << std::get<Instruction *>(data)->id << ' ';
    else if (std::holds_alternative<int>(data))
        std::cout << std::get<int>(data) << ' ';
    else if (std::holds_alternative<PhiInput>(data)) {
        auto &phid = std::get<PhiInput>(data);
        std::cout << "[%" << phid.first->id << ", %" << phid.second->id << "] ";
    } else
        std::cout << "idk what this is in input :((( ";
}

}  // namespace IR
}  // namespace Compiler
