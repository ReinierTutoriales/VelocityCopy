#include "velocitycopy/strategy_selector.hpp"

#include <windows.h>

int wmain() {
    velocitycopy::StrategySelector selector;

    velocitycopy::StorageProfile local{};
    local.kind = velocitycopy::StorageKind::Fixed;
    local.sector_info_available = true;
    local.logical_sector_bytes = 4096;
    local.physical_sector_bytes = 4096;
    local.seek_penalty_available = true;
    local.incurs_seek_penalty = false;

    auto rotational = local;
    rotational.incurs_seek_penalty = true;

    velocitycopy::StorageProfile network{};
    network.kind = velocitycopy::StorageKind::Network;
    network.remote = true;

    const auto large = selector.choose(local, local, {
        8ull * 1024ull * 1024ull * 1024ull,
        1,
        8ull * 1024ull * 1024ull * 1024ull,
    });
    if (large.strategy != velocitycopy::CopyStrategyKind::WindowsCopyFile2 ||
        (large.copy_flags & COPY_FILE_NO_BUFFERING) != 0 || !large.async_iocp_candidate ||
        large.suggested_queue_depth != 1 || large.suggested_buffer_bytes != 4u * 1024u * 1024u) {
        return 1;
    }

    auto unknown_source_geometry = local;
    unknown_source_geometry.sector_info_available = false;
    const auto large_unknown_source_geometry = selector.choose(unknown_source_geometry, local, {
        8ull * 1024ull * 1024ull * 1024ull,
        1,
        8ull * 1024ull * 1024ull * 1024ull,
    });
    if (large_unknown_source_geometry.strategy != velocitycopy::CopyStrategyKind::WindowsCopyFile2 ||
        (large_unknown_source_geometry.copy_flags & COPY_FILE_NO_BUFFERING) != 0 ||
        large_unknown_source_geometry.suggested_queue_depth != 1) {
        return 7;
    }

    const auto small = selector.choose(local, local, {
        10000ull * 4096ull,
        10000,
        4096,
    });
    if (small.strategy != velocitycopy::CopyStrategyKind::WindowsCopyFile2 ||
        !small.async_iocp_candidate || small.suggested_queue_depth != 2) {
        return 2;
    }

    const auto remote = selector.choose(network, network, {1024, 1, 1024});
    if ((remote.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) == 0 ||
        remote.async_iocp_candidate || remote.suggested_queue_depth != 1) {
        return 3;
    }

    const auto normal = selector.choose(local, local, {
        512ull * 1024ull * 1024ull,
        8,
        64ull * 1024ull * 1024ull,
    });
    if (!normal.async_iocp_candidate || normal.suggested_queue_depth != 4) {
        return 4;
    }

    const auto hdd = selector.choose(rotational, local, {
        512ull * 1024ull * 1024ull,
        8,
        64ull * 1024ull * 1024ull,
    });
    if (hdd.async_iocp_candidate || hdd.suggested_queue_depth != 1) {
        return 5;
    }

    auto unknown_seek = local;
    unknown_seek.seek_penalty_available = false;
    const auto unknown = selector.choose(unknown_seek, local, {
        512ull * 1024ull * 1024ull,
        8,
        64ull * 1024ull * 1024ull,
    });
    if (unknown.async_iocp_candidate || unknown.suggested_queue_depth != 1) {
        return 6;
    }

    return 0;
}
