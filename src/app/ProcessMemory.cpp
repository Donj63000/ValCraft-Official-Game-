#include "app/ProcessMemory.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace valcraft {

auto query_process_memory() noexcept -> ProcessMemorySnapshot {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters {};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            static_cast<DWORD>(sizeof(counters))) == FALSE) {
        return {};
    }

    return {
        static_cast<std::uint64_t>(counters.WorkingSetSize),
        static_cast<std::uint64_t>(counters.PrivateUsage),
        true,
    };
#elif defined(__linux__)
    const auto read_kib_value = [](const std::string& line) noexcept -> std::uint64_t {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            return 0U;
        }
        try {
            return std::stoull(line.substr(colon + 1U));
        } catch (...) {
            return 0U;
        }
    };

    auto working_set_kib = std::uint64_t {0};
    auto private_kib = std::uint64_t {0};
    std::ifstream rollup("/proc/self/smaps_rollup");
    std::string line;
    if (rollup) {
        while (std::getline(rollup, line)) {
            if (line.starts_with("Rss:")) {
                working_set_kib = read_kib_value(line);
            } else if (line.starts_with("Private_Clean:") || line.starts_with("Private_Dirty:") ||
                       line.starts_with("Private_Hugetlb:")) {
                private_kib += read_kib_value(line);
            }
        }
    }

    if (working_set_kib == 0U) {
        std::ifstream status("/proc/self/status");
        while (std::getline(status, line)) {
            if (line.starts_with("VmRSS:")) {
                working_set_kib = read_kib_value(line);
            } else if (line.starts_with("RssAnon:")) {
                private_kib = read_kib_value(line);
            }
        }
    }

    constexpr auto kBytesPerKibibyte = std::uint64_t {1024};
    return {
        working_set_kib * kBytesPerKibibyte,
        private_kib * kBytesPerKibibyte,
        working_set_kib > 0U,
    };
#elif defined(__APPLE__)
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            TASK_VM_INFO,
            reinterpret_cast<task_info_t>(&info),
            &count) != KERN_SUCCESS) {
        return {};
    }
    return {
        static_cast<std::uint64_t>(info.resident_size),
        static_cast<std::uint64_t>(info.phys_footprint),
        true,
    };
#else
    return {};
#endif
}

} // namespace valcraft
