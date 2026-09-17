#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/storage_profiler.hpp"
#include "velocitycopy/strategy_selector.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>

namespace {

const wchar_t* storage_name(const velocitycopy::StorageKind kind) noexcept {
    using velocitycopy::StorageKind;
    switch (kind) {
    case StorageKind::Fixed: return L"Fixed";
    case StorageKind::Removable: return L"Removable";
    case StorageKind::Network: return L"Network";
    case StorageKind::Optical: return L"Optical";
    case StorageKind::RamDisk: return L"RAM disk";
    default: return L"Unknown";
    }
}

const wchar_t* strategy_name(const velocitycopy::CopyStrategyKind kind) noexcept {
    return kind == velocitycopy::CopyStrategyKind::WindowsCopyFile2NoBuffering
        ? L"CopyFile2 (unbuffered)"
        : L"CopyFile2 (buffered)";
}

const wchar_t* seek_name(const velocitycopy::StorageProfile& profile) noexcept {
    if (!profile.seek_penalty_available) return L"unknown";
    return profile.incurs_seek_penalty ? L"rotational/seek penalty" : L"nonrotational";
}

velocitycopy::CopyStrategyKind executed_strategy(const std::uint32_t copy_flags) noexcept {
    return (copy_flags & COPY_FILE_NO_BUFFERING) != 0
        ? velocitycopy::CopyStrategyKind::WindowsCopyFile2NoBuffering
        : velocitycopy::CopyStrategyKind::WindowsCopyFile2;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3) {
        std::wcout << L"Usage: VelocityCopyBenchmark <source> <destination>\n";
        return 1;
    }

    velocitycopy::CopyJob job{};
    job.id = 1;
    job.sources = {std::filesystem::path(argv[1])};
    job.destination = std::filesystem::path(argv[2]);
    job.layout = velocitycopy::DestinationLayout::ContentsOnly;

    velocitycopy::JobPlanner planner;
    velocitycopy::CopyPlan plan;
    try {
        plan = planner.build(job);
    } catch (const std::exception&) {
        std::wcerr << L"Unable to build copy plan.\n";
        return 2;
    }

    velocitycopy::WorkloadProfile workload{};
    workload.total_bytes = plan.total_bytes;
    workload.file_count = plan.files.size();
    for (const auto& file : plan.files) {
        workload.largest_file_bytes = std::max(workload.largest_file_bytes, file.size);
    }

    velocitycopy::StorageProfiler profiler;
    const auto source_profile = profiler.inspect(job.sources.front());
    const auto destination_profile = profiler.inspect(job.destination);

    velocitycopy::StrategySelector selector;
    const auto recommendation = selector.choose(source_profile, destination_profile, workload);

    std::wcout << L"Source: " << storage_name(source_profile.kind)
               << L" | " << seek_name(source_profile)
               << L" | sector " << source_profile.logical_sector_bytes << L"/"
               << source_profile.physical_sector_bytes << L"\n";
    std::wcout << L"Destination: " << storage_name(destination_profile.kind)
               << L" | " << seek_name(destination_profile)
               << L" | sector " << destination_profile.logical_sector_bytes << L"/"
               << destination_profile.physical_sector_bytes << L"\n";
    std::wcout << L"Strategy candidate: " << strategy_name(recommendation.strategy)
               << L" | suggested QD " << recommendation.suggested_queue_depth
               << L" | async candidate " << (recommendation.async_iocp_candidate ? L"yes" : L"no") << L"\n";

    const auto total_bytes = plan.total_bytes;
    velocitycopy::LiveCopyPlan live_plan(std::move(plan));
    velocitycopy::JobExecutor executor;
    velocitycopy::ExecutionControl control;
    const auto execution_options = executor.recommend_options(live_plan);
    const auto production_strategy = executed_strategy(execution_options.copy_flags);

    std::wcout << L"Production execution: " << strategy_name(production_strategy)
               << L" | workers " << execution_options.worker_count
               << L" | flags 0x" << std::hex << execution_options.copy_flags << std::dec
               << L" | compressed traffic "
               << (((execution_options.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) != 0) ? L"yes" : L"no")
               << L"\n";

    const auto start = std::chrono::steady_clock::now();
    const auto result = executor.execute(live_plan, control, execution_options);
    const auto end = std::chrono::steady_clock::now();

    if (!result.success) {
        std::wcerr << L"Benchmark copy failed. Windows status: 0x"
                   << std::hex << static_cast<std::uint32_t>(result.native_code) << L"\n";
        return 3;
    }

    const std::chrono::duration<double> elapsed = end - start;
    const double mib = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    const double mib_per_second = elapsed.count() > 0.0 ? mib / elapsed.count() : 0.0;

    std::wcout << std::fixed << std::setprecision(2)
               << L"Copied " << mib << L" MiB in " << elapsed.count()
               << L" s (" << mib_per_second << L" MiB/s)\n";
    return 0;
}
