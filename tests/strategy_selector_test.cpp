#include "velocitycopy/strategy_selector.hpp"

#include <windows.h>

int wmain() {
    velocitycopy::StrategySelector selector;

    velocitycopy::StorageProfile local{};
    local.kind = velocitycopy::StorageKind::Fixed;
    local.sector_info_available = true;
    local.logical_sector_bytes = 4096;
    local.physical_sector_bytes = 4096;

    velocitycopy::StorageProfile network{};
    network.kind = velocitycopy::StorageKind::Network;
    network.remote = true;

    const auto large = selector.choose(local, local, {
        8ull * 1024ull * 1024ull * 1024ull,
        1,
        8ull * 1024ull * 1024ull * 1024ull,
    });
    if (large.strategy != velocitycopy::CopyStrategyKind::WindowsCopyFile2NoBuffering ||
        (large.copy_flags & COPY_FILE_NO_BUFFERING) == 0 || !large.async_iocp_candidate) {
        return 1;
    }

    const auto small = selector.choose(local, local, {
        10000ull * 4096ull,
        10000,
        4096,
    });
    if (small.strategy != velocitycopy::CopyStrategyKind::WindowsCopyFile2 || small.async_iocp_candidate) {
        return 2;
    }

    const auto remote = selector.choose(network, network, {1024, 1, 1024});
    if ((remote.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) == 0 || remote.async_iocp_candidate) {
        return 3;
    }

    const auto normal = selector.choose(local, local, {
        512ull * 1024ull * 1024ull,
        8,
        64ull * 1024ull * 1024ull,
    });
    if (!normal.async_iocp_candidate || normal.suggested_queue_depth < 2) {
        return 4;
    }

    return 0;
}
