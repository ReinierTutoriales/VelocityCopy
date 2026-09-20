#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/storage_profiler.hpp"
#include "velocitycopy/storage_topology.hpp"
#include "velocitycopy/strategy_selector.hpp"

#include <windows.h>
#include <psapi.h>

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
    const auto topology = velocitycopy::physical_storage_relationship(source_profile, destination_profile);
    const bool topology_known = topology != velocitycopy::PhysicalStorageRelationship::Unknown;
    const bool shared_physical_disk = topology == velocitycopy::PhysicalStorageRelationship::SharedDisk;
    const wchar_t* topology_scenario = !topology_known ? L"unknown" : (shared_physical_disk ? L"G" : L"H");

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
        std::wcout << L"Strategy candidate: CopyFile2 (buffered)"
                   << L" | suggested QD " << recommendation.suggested_queue_depth
                   << L" | I/O request " << recommendation.suggested_buffer_bytes << L" bytes\n";
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
        std::wcout << L"Production execution: CopyFile2 (buffered)"
                   << L" | workers " << execution_options.worker_count
                   << L" | recommended workers " << recommended_workers
                   << L" | flags 0x" << std::hex << execution_options.copy_flags << std::dec
                   << L" | I/O request " << execution_options.suggested_buffer_bytes << L" bytes"
                   << L" | compressed traffic "
                   << (((execution_options.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) != 0) ? L"yes" : L"no")
                   << L"\n";
    }

    FILETIME create_time{}, exit_time{}, kernel_start{}, user_start{}, kernel_end{}, user_end{};
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    (void)GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_start, &user_start);
    const auto start = std::chrono::steady_clock::now();
    const auto result = executor.execute(live_plan, control, execution_options);
    const auto end = std::chrono::steady_clock::now();
    (void)GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_end, &user_end);
    (void)GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));

    if (!result.success) {
        std::wcerr << L"Benchmark copy failed. Windows status: 0x"
                   << std::hex << static_cast<std::uint32_t>(result.native_code) << L"\n";
        return 3;
    }

    const std::chrono::duration<double> elapsed = end - start;
    const double mib = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    const double mib_per_second = elapsed.count() > 0.0 ? mib / elapsed.count() : 0.0;
    const double files_per_second = elapsed.count() > 0.0
        ? static_cast<double>(workload.file_count) / elapsed.count()
        : 0.0;
    const auto filetime_value = [](const FILETIME& value) noexcept -> std::uint64_t {
        ULARGE_INTEGER ticks{}; ticks.LowPart = value.dwLowDateTime; ticks.HighPart = value.dwHighDateTime; return ticks.QuadPart;
    };
    const double cpu_seconds = static_cast<double>((filetime_value(kernel_end) - filetime_value(kernel_start)) + (filetime_value(user_end) - filetime_value(user_start))) / 10000000.0;
    const double cpu_cores_used = elapsed.count() > 0.0 ? cpu_seconds / elapsed.count() : 0.0;

    if (json_output) {
        std::wcout << std::fixed << std::setprecision(6)
                   << L"{\"source_kind\":\"" << storage_name(source_profile.kind)
                   << L"\",\"destination_kind\":\"" << storage_name(destination_profile.kind)
                   << L"\",\"source_seek\":\"" << seek_name(source_profile)
                   << L"\",\"destination_seek\":\"" << seek_name(destination_profile)
                   << L"\",\"source_topology_known\":" << (source_profile.physical_disk_extents_available ? L"true" : L"false")
                   << L",\"destination_topology_known\":" << (destination_profile.physical_disk_extents_available ? L"true" : L"false")
                   << L",\"shared_physical_disk\":" << (shared_physical_disk ? L"true" : L"false")
                   << L",\"topology_scenario\":\"" << topology_scenario << L"\""
                   << L",\"total_bytes\":" << total_bytes
                   << L",\"file_count\":" << workload.file_count
                   << L",\"largest_file_bytes\":" << workload.largest_file_bytes
                   << L",\"strategy\":\"CopyFile2 (buffered)\""
                   << L",\"workers\":" << execution_options.worker_count
                   << L",\"recommended_workers\":" << recommended_workers
                   << L",\"workers_overridden\":" << (forced_workers != 0 ? L"true" : L"false")
                   << L",\"copy_flags\":" << execution_options.copy_flags
                   << L",\"buffer_bytes\":" << execution_options.suggested_buffer_bytes
                   << L",\"compressed_traffic\":"
                   << (((execution_options.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC) != 0) ? L"true" : L"false")
                   << L",\"elapsed_seconds\":" << elapsed.count()
                   << L",\"mib_per_second\":" << mib_per_second
                   << L",\"files_per_second\":" << files_per_second
                   << L",\"cpu_seconds\":" << cpu_seconds
                   << L",\"cpu_cores_used\":" << cpu_cores_used
                   << L",\"working_set_bytes\":" << memory.WorkingSetSize
                   << L",\"peak_working_set_bytes\":" << memory.PeakWorkingSetSize
                   << L",\"private_usage_bytes\":" << memory.PrivateUsage
                   << L"}\n";
    } else {
        std::wcout << std::fixed << std::setprecision(2)
                   << L"Copied " << mib << L" MiB in " << elapsed.count()
                   << L" s (" << mib_per_second << L" MiB/s, "
                   << files_per_second << L" files/s)\n";
    }
    return 0;
}
