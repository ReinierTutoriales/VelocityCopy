#include "velocitycopy/strategy_selector.hpp"

#include <windows.h>

#include <algorithm>

namespace velocitycopy {

StrategyRecommendation StrategySelector::choose(
    const StorageProfile& source,
    const StorageProfile& destination,
    const WorkloadProfile& workload) const noexcept {
    StrategyRecommendation recommendation{};

    constexpr std::uint64_t one_gib = 1024ull * 1024ull * 1024ull;
    constexpr std::uint64_t small_file_average = 256ull * 1024ull;

    const bool network = source.remote || destination.remote;
    const bool many_small_files = workload.file_count >= 10000 ||
        (workload.file_count != 0 && workload.total_bytes / workload.file_count <= small_file_average);
    const bool very_large_file = workload.largest_file_bytes >= one_gib;
    const bool local_fixed = source.kind == StorageKind::Fixed && destination.kind == StorageKind::Fixed && !network;
    const bool known_nonrotational =
        source.seek_penalty_available && destination.seek_penalty_available &&
        !source.incurs_seek_penalty && !destination.incurs_seek_penalty;

    if (network) {
        recommendation.copy_flags = COPY_FILE_REQUEST_COMPRESSED_TRAFFIC;
        recommendation.suggested_queue_depth = 1;
        return recommendation;
    }

    if (very_large_file && local_fixed && destination.sector_info_available) {
        recommendation.strategy = CopyStrategyKind::WindowsCopyFile2NoBuffering;
        recommendation.copy_flags = COPY_FILE_NO_BUFFERING;
        recommendation.suggested_buffer_bytes = 4u * 1024u * 1024u;
        recommendation.suggested_queue_depth = 1;
        recommendation.async_iocp_candidate = known_nonrotational;
        return recommendation;
    }

    if (local_fixed && known_nonrotational) {
        recommendation.async_iocp_candidate = true;
        recommendation.suggested_buffer_bytes = many_small_files
            ? 512u * 1024u
            : 2u * 1024u * 1024u;
        recommendation.suggested_queue_depth = many_small_files ? 2u : 4u;
    }

    return recommendation;
}

} // namespace velocitycopy
