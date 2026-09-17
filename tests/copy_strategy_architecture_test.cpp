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
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(const int code, const char* message) {
    std::cerr << "strategy architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto selector_h = read_all(root / "src/core/include/velocitycopy/strategy_selector.hpp");
    const auto selector_cpp = read_all(root / "src/core/strategy_selector.cpp");
    const auto executor_h = read_all(root / "src/core/include/velocitycopy/job_executor.hpp");
    const auto executor_cpp = read_all(root / "src/core/job_executor.cpp");
    const auto engine_h = read_all(root / "src/core/include/velocitycopy/copy_engine.hpp");
    const auto engine_cpp = read_all(root / "src/core/copy_engine.cpp");
    const auto benchmark_cpp = read_all(root / "tools/benchmark.cpp");

    if (selector_h.empty() || selector_cpp.empty() || executor_h.empty() || executor_cpp.empty() ||
        engine_h.empty() || engine_cpp.empty() || benchmark_cpp.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(selector_h, "std::uint32_t copy_flags{}") ||
        !contains(selector_cpp, "recommendation.copy_flags = COPY_FILE_NO_BUFFERING") ||
        !contains(selector_cpp, "recommendation.copy_flags = COPY_FILE_REQUEST_COMPRESSED_TRAFFIC")) {
        return fail(2, "strategy selector must emit native CopyFile2 flags");
    }

    if (!contains(executor_h, "std::uint32_t copy_flags{}") ||
        !contains(executor_cpp, "options.copy_flags = shared_copy_flags") ||
        !contains(executor_cpp, "CopyOptions{resume_from_pause, existing_policy, options.copy_flags}")) {
        return fail(3, "JobExecutor must carry selected flags into the live production copy path");
    }

    if (!contains(engine_h, "std::uint32_t copy_flags{}") ||
        !contains(engine_cpp, "parameters.dwCopyFlags = options.copy_flags")) {
        return fail(4, "CopyEngine must consume the selected native flags");
    }

    if (!contains(executor_cpp, "shared_copy_flags &= recommendation.copy_flags")) {
        return fail(5, "multi-root execution must keep only flags supported by every source root");
    }

    if (!contains(benchmark_cpp, "executed_strategy(execution_options.copy_flags)") ||
        !contains(benchmark_cpp, "execution_options.copy_flags & COPY_FILE_REQUEST_COMPRESSED_TRAFFIC") ||
        contains(benchmark_cpp, "Production execution: CopyFile2 buffered baseline")) {
        return fail(6, "benchmark must report the strategy and native flags actually used by production execution");
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
