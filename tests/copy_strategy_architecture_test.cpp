#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(const int code, const char* message) {
    std::cerr << "strategy architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto selector_h = read_all(root / "src/core/include/velocitycopy/strategy_selector.hpp");
    const auto selector_cpp = read_all(root / "src/core/strategy_selector.cpp");
    const auto executor_h = read_all(root / "src/core/include/velocitycopy/job_executor.hpp");
    const auto executor_cpp = read_all(root / "src/core/job_executor.cpp");
    const auto engine_h = read_all(root / "src/core/include/velocitycopy/copy_engine.hpp");
    const auto engine_cpp = read_all(root / "src/core/copy_engine.cpp");
    const auto benchmark_cpp = read_all(root / "tools/benchmark.cpp");
    const auto profiler_h = read_all(root / "src/core/include/velocitycopy/storage_profiler.hpp");
    const auto profiler_cpp = read_all(root / "src/core/storage_profiler.cpp");
    const auto topology_h = read_all(root / "src/core/include/velocitycopy/storage_topology.hpp");
    const auto topology_cpp = read_all(root / "src/core/storage_topology.cpp");

    if (selector_h.empty() || selector_cpp.empty() || executor_h.empty() || executor_cpp.empty() ||
        engine_h.empty() || engine_cpp.empty() || benchmark_cpp.empty() || profiler_h.empty() || profiler_cpp.empty() ||
        topology_h.empty() || topology_cpp.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(profiler_h, "std::uint32_t device_number{}") ||
        !contains(profiler_h, "bool device_number_available{}") ||
        !contains(profiler_cpp, "GetVolumeNameForVolumeMountPointW") ||
        !contains(profiler_cpp, "IOCTL_STORAGE_GET_DEVICE_NUMBER") ||
        !contains(profiler_cpp, "device_path.pop_back()") ||
        !contains(profiler_h, "std::vector<std::uint32_t> physical_disk_numbers") ||
        !contains(profiler_h, "bool physical_disk_extents_available{}") ||
        !contains(profiler_cpp, "IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS") ||
        !contains(profiler_cpp, "ERROR_MORE_DATA")) {
        return fail(11, "storage profiling must resolve mounted volumes and expose physical disk topology");
    }

    if (!contains(topology_h, "enum class PhysicalStorageRelationship") ||
        !contains(topology_h, "Unknown") || !contains(topology_h, "SharedDisk") || !contains(topology_h, "DisjointDisks") ||
        !contains(topology_cpp, "PhysicalStorageRelationship::Unknown") ||
        !contains(topology_cpp, "PhysicalStorageRelationship::SharedDisk") ||
        !contains(topology_cpp, "PhysicalStorageRelationship::DisjointDisks")) {
        return fail(12, "physical storage topology must preserve unknown/shared/disjoint semantics");
    }

    if (!contains(selector_h, "std::uint32_t copy_flags{}") ||
        !contains(selector_h, "std::uint32_t suggested_buffer_bytes{}") ||
        !contains(selector_h, "std::uint32_t suggested_queue_depth{1}") ||
        contains(selector_h, "CopyStrategyKind") ||
        contains(selector_h, "async_iocp_candidate") ||
        contains(selector_cpp, "recommendation.copy_flags = COPY_FILE_NO_BUFFERING") ||
        !contains(selector_cpp, "recommendation.copy_flags = COPY_FILE_REQUEST_COMPRESSED_TRAFFIC") ||
        !contains(selector_cpp, "buffered CopyFile2")) {
        return fail(2, "selector must expose only strategy data consumed by buffered CopyFile2 production execution");
    }

    if (!contains(executor_h, "std::uint32_t copy_flags{}") ||
        !contains(executor_h, "std::uint32_t suggested_buffer_bytes{}") ||
        contains(executor_h, "CopyStrategyKind") || contains(executor_h, "async_iocp_candidate") ||
        !contains(executor_cpp, "options.copy_flags = shared_copy_flags") ||
        !contains(executor_cpp, "options.suggested_buffer_bytes = shared_buffer_bytes") ||
        !contains(executor_cpp, "options.suggested_buffer_bytes,") ||
        !contains(executor_cpp, "physical_storage_relationship(source, destination) == PhysicalStorageRelationship::SharedDisk") ||
        !contains(executor_cpp, "source_destination_share_disk") ||
        !contains(executor_cpp, "worker_count = 1")) {
        return fail(3, "JobExecutor must carry selected flags, I/O size and topology limits into production execution");
    }

    if (!contains(engine_h, "std::uint32_t copy_flags{}") ||
        !contains(engine_h, "std::uint32_t io_size_bytes{}") ||
        !contains(engine_cpp, "parameters.dwCopyFlags = options.copy_flags") ||
        !contains(engine_cpp, "desired_io_size(source_size, options.io_size_bytes)") ||
        !contains(engine_cpp, "parameters.ioDesiredSize")) {
        return fail(4, "CopyEngine must consume selected flags and I/O request size");
    }

    if (!contains(executor_cpp, "shared_copy_flags &= recommendation.copy_flags") ||
        !contains(executor_cpp, "shared_buffer_bytes = std::min(shared_buffer_bytes, recommendation.suggested_buffer_bytes)")) {
        return fail(5, "multi-root execution must conservatively intersect strategy recommendations");
    }

    if (!contains(benchmark_cpp, "execution_options.suggested_buffer_bytes") ||
        !contains(benchmark_cpp, "CopyFile2 (buffered)") ||
        contains(benchmark_cpp, "async_candidate") ||
        contains(benchmark_cpp, "strategy_name(")) {
        return fail(6, "benchmark must report only strategy metadata actually consumed by production execution");
    }

    if (!contains(executor_cpp, "current == ExecutionDirective::Stop") ||
        !contains(executor_cpp, "return CopyDecision::Stop") ||
        !contains(executor_cpp, "aborted && control.directive() == ExecutionDirective::Stop")) {
        return fail(13, "Stop must be observed inside an active CopyFile2 operation and preserve the live plan");
    }

    const auto copy_plan_overload = executor_cpp.find("JobResult JobExecutor::execute(\n    const CopyPlan& plan");
    const auto live_plan_overload = executor_cpp.find("JobResult JobExecutor::execute(\n    LiveCopyPlan& plan");
    if (copy_plan_overload == std::string::npos || live_plan_overload == std::string::npos ||
        copy_plan_overload >= live_plan_overload) {
        return fail(7, "CopyPlan compatibility adapter and LiveCopyPlan production path must both exist");
    }

    const auto adapter = executor_cpp.substr(copy_plan_overload, live_plan_overload - copy_plan_overload);
    if (!contains(adapter, "LiveCopyPlan live_plan(plan)") ||
        !contains(adapter, "return execute(live_plan, progress)") ||
        contains(adapter, "engine_.copy_file") ||
        contains(adapter, "std::filesystem::create_directories")) {
        return fail(8, "CopyPlan execution must delegate to LiveCopyPlan instead of owning a second copy loop");
    }

    const auto copy_job_overload = executor_cpp.find("JobResult JobExecutor::execute(\n    const CopyJob& job");
    if (copy_job_overload == std::string::npos || copy_job_overload >= copy_plan_overload) {
        return fail(9, "CopyJob entry point must remain available before the CopyPlan adapter");
    }
    const auto job_adapter = executor_cpp.substr(copy_job_overload, copy_plan_overload - copy_job_overload);
    if (!contains(job_adapter, "LiveCopyPlan live_plan(std::move(plan))") ||
        !contains(job_adapter, "return execute(live_plan, progress)")) {
        return fail(10, "CopyJob execution must enter the same LiveCopyPlan production path");
    }

    return 0;
}
