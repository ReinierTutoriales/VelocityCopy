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
#include <stdexcept>
#include <string>
#include <string_view>

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

void write_disk_numbers(std::wostream& stream, const velocitycopy::StorageProfile& profile) {
    if (!profile.physical_disk_extents_available) {
        stream << L"unknown";
        return;
    }
    for (std::size_t index = 0; index < profile.physical_disk_numbers.size(); ++index) {
        if (index != 0) stream << L",";
        stream << profile.physical_disk_numbers[index];
    }
}

bool shares_physical_disk(
    const velocitycopy::StorageProfile& source,
    const velocitycopy::StorageProfile& destination) noexcept {
    if (!source.physical_disk_extents_available || !destination.physical_disk_extents_available) {
        return false;
    }
    for (const auto disk : source.physical_disk_numbers) {
        if (std::find(destination.physical_disk_numbers.begin(), destination.physical_disk_numbers.end(), disk) !=
            destination.physical_disk_numbers.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    bool json_output = false;
    std::uint32_t forced_workers = 0;
    for (int index = 3; index < argc; ++index) {
        const std::wstring_view arg(argv[index]);
        if (arg == L"--json") {
            json_output = true;
        } else if (arg.starts_with(L"--workers=")) {
            try {
                const auto parsed = std::stoul(std::wstring(arg.substr(10)));
                if (parsed < 1 || parsed > 4) throw std::out_of_range("workers");
                forced_workers = static_cast<std::uint32_t>(parsed);
            } catch (...) {
                std::wcerr << L"Invalid --workers value; supported range is 1..4.\n";
                return 1;
            }
        } else {
            std::wcerr << L"Unknown benchmark option.\n";
            return 1;
        }
    }
    if (argc < 3) {
        std::wcout << L"Usage: VelocityCopyBenchmark <source> <destination> [--workers=1..4] [--json]\n";
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
    const bool topology_known = source_profile.physical_disk_extents_available && destination_profile.physical_disk_extents_available;
    const bool shared_physical_disk = topology_known && shares_physical_disk(source_profile, destination_profile);

    velocitycopy::StrategySelector selector;
    const auto recommendation = selector.choose(source_profile, destination_profile, workload);

    if (!json_output) {
        std::wcout << L"Source: " << storage_name(source_profile.kind)
                   << L" | " << seek_name(source_profile)
                   << L" | sector " << source_profile.logical_sector_bytes << L"/"
                   << source_profile.physical_sector_bytes << L" | disks ";
        write_disk_numbers(std::wcout, source_profile);
        std::wcout << L"\n";
        std::wcout << L"Destination: " << storage_name(destination_profile.kind)
                   << L" | " << seek_name(destination_profile)
                   << L" | sector " << destination_profile.logical_sector_bytes << L"/"
                   << destination_profile.physical_sector_bytes << L" | disks ";
        write_disk_numbers(std::wcout, destination_profile);
        std::wcout << L" | shared physical disk " << (shared_physical_disk ? L"yes" : (topology_known ? L"no" : L"unknown")) << L"\n";
        std::wcout << L"Strategy candidate: " << strategy_name(recommendation.strategy)
                   << L" | suggested QD " << recommendation.suggested_queue_depth
                   << L" | buffer " << recommendation.suggested_buffer_bytes
                   << L" | async candidate " << (recommendation.async_iocp_candidate ? L"yes" : L"no") << L"\n";
    }

    const auto total_bytes = plan.total_bytes;
    velocitycopy::LiveCopyPlan live_plan(std::move(plan));
    velocitycopy::JobExecutor executor;
    velocitycopy::ExecutionControl control;
    auto execution_options = executor.recommend_options(live_plan);
    const auto recommended_workers = execution_options.worker_count;
    if (forced_workers != 0) {
        execution_options.worker_count = forced_workers;
    }

    if (!json_output) {
        std::wcout << L"Production execution: " << strategy_name(execution_options.strategy)
                   << L" | workers " << execution_options.worker_count
                   << L" | recommended workers " << recommended_workers
                   << L" | flags 0x" << std::hex << execution_options.copy_flags << std::dec
                   << L" | buffer " << execution_options.suggested_buffer_bytes
                   << L" | async candidate " << (execution_options.async_iocp_candidate ? L"yes" : L"no")
                   << L" | compressed traffic "
                   << (((execution_options.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) != 0) ? L"yes" : L"no")
                   << L"\n";
    }

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

    if (json_output) {
        std::wcout << std::fixed << std::setprecision(6)
                   << L"{\"source_kind\":\"" << storage_name(source_profile.kind)
                   << L"\",\"destination_kind\":\"" << storage_name(destination_profile.kind)
                   << L"\",\"source_seek\":\"" << seek_name(source_profile)
                   << L"\",\"destination_seek\":\"" << seek_name(destination_profile)
                   << L"\",\"source_topology_known\":" << (source_profile.physical_disk_extents_available ? L"true" : L"false")
                   << L",\"destination_topology_known\":" << (destination_profile.physical_disk_extents_available ? L"true" : L"false")
                   << L",\"shared_physical_disk\":" << (shared_physical_disk ? L"true" : L"false")
                   << L",\"total_bytes\":" << total_bytes
                   << L",\"file_count\":" << workload.file_count
                   << L",\"largest_file_bytes\":" << workload.largest_file_bytes
                   << L",\"strategy\":\"" << strategy_name(execution_options.strategy)
                   << L"\",\"workers\":" << execution_options.worker_count
                   << L",\"recommended_workers\":" << recommended_workers
                   << L",\"workers_overridden\":" << (forced_workers != 0 ? L"true" : L"false")
                   << L",\"copy_flags\":" << execution_options.copy_flags
                   << L",\"buffer_bytes\":" << execution_options.suggested_buffer_bytes
                   << L",\"async_candidate\":" << (execution_options.async_iocp_candidate ? L"true" : L"false")
                   << L",\"compressed_traffic\":"
                   << (((execution_options.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) != 0) ? L"true" : L"false")
                   << L",\"elapsed_seconds\":" << elapsed.count()
                   << L",\"mib_per_second\":" << mib_per_second
                   << L"}\n";
    } else {
        std::wcout << std::fixed << std::setprecision(2)
                   << L"Copied " << mib << L" MiB in " << elapsed.count()
                   << L" s (" << mib_per_second << L" MiB/s)\n";
    }
    return 0;
}
