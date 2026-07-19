#pragma once

#include <cstdint>

namespace valcraft {

struct ProcessMemorySnapshot {
    std::uint64_t working_set_bytes = 0;
    std::uint64_t private_bytes = 0;
    bool valid = false;
};

[[nodiscard]] auto query_process_memory() noexcept -> ProcessMemorySnapshot;

} // namespace valcraft
