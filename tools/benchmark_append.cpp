#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 4) {
        std::wcout << L"Usage: VelocityCopyAppendBenchmark <initial-source> <append-source> <destination>\n";
        return 1;
    }

    namespace fs = std::filesystem;
    using namespace velocitycopy;

    CopyJob initial_job{};
    initial_job.id = 1;
    initial_job.sources = {fs::path(argv[1])};
    initial_job.destination = fs::path(argv[3]);
    initial_job.layout = DestinationLayout::ContentsOnly;

    CopyJob append_job = initial_job;
    append_job.id = 2;
    append_job.sources = {fs::path(argv[2])};

    JobPlanner planner;
    CopyPlan initial;
    CopyPlan extra;
    try {
        initial = planner.build(initial_job);
        extra = planner.build(append_job);
    } catch (...) {
        std::wcerr << L"Unable to build append benchmark plans.\n";
        return 2;
    }

    const auto initial_files = initial.files.size();
    const auto appended_files = extra.files.size();
    const auto initial_bytes = initial.total_bytes;
    const auto appended_bytes = extra.total_bytes;
    LiveCopyPlan live(std::move(initial));
    JobExecutor executor;
    ExecutionControl control;
    auto options = executor.recommend_options(live);

    bool appended = false;
    double append_latency_ms = 0.0;
    const auto start = std::chrono::steady_clock::now();
    const auto result = executor.execute(live, control, options, [&](const JobProgress& progress) {
        if (!appended && progress.completed_files >= 1) {
            const auto append_start = std::chrono::steady_clock::now();
            const auto append_result = live.append(std::move(extra));
            const auto append_end = std::chrono::steady_clock::now();
            append_latency_ms = std::chrono::duration<double, std::milli>(append_end - append_start).count();
            if (append_result != LivePlanAppendResult::Appended) return JobDecision::Cancel;
            appended = true;
        }
        return JobDecision::Continue;
    });
    const auto end = std::chrono::steady_clock::now();

    if (!result.success || !appended) {
        std::wcerr << L"Append benchmark failed.\n";
        return 3;
    }

    const double elapsed = std::chrono::duration<double>(end - start).count();
    const auto total_bytes = initial_bytes + appended_bytes;
    const auto total_files = initial_files + appended_files;
    const double mibps = elapsed > 0.0 ? (static_cast<double>(total_bytes) / (1024.0 * 1024.0)) / elapsed : 0.0;
    const double filesps = elapsed > 0.0 ? static_cast<double>(total_files) / elapsed : 0.0;

    std::wcout << std::fixed << std::setprecision(6)
               << L"{\"scenario\":\"F\",\"initial_files\":" << initial_files
               << L",\"appended_files\":" << appended_files
               << L",\"total_bytes\":" << total_bytes
               << L",\"workers\":" << options.worker_count
               << L",\"append_latency_ms\":" << append_latency_ms
               << L",\"elapsed_seconds\":" << elapsed
               << L",\"mib_per_second\":" << mibps
               << L",\"files_per_second\":" << filesps << L"}\n";
    return 0;
}
