#pragma once

#include "velocitycopy/storage_profiler.hpp"

#include <cstdint>

namespace velocitycopy {

struct WorkloadProfile {
    std::uint64_t total_bytes{};
    std::uint64_t file_count{};
    std::uint64_t largest_file_bytes{};
};

struct StrategyRecommendation {
    std::uint32_t copy_flags{};
    std::uint32_t suggested_buffer_bytes{};
    std::uint32_t suggested_queue_depth{1};
};

class StrategySelector final {
public:
    [[nodiscard]] StrategyRecommendation choose(
        const StorageProfile& source,
        const StorageProfile& destination,
        const WorkloadProfile& workload) const noexcept;
};

} // namespace velocitycopy
