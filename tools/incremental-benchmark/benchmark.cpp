#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace velocitycopy;
using Clock = std::chrono::steady_clock;

std::uint64_t ticks(const FILETIME time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 1;
    try {
        const std::wstring mode = argv[1];
        const fs::path root = argv[2];
        const auto workers = static_cast<std::uint32_t>(std::stoul(argv[3]));
        if (workers < 1 || workers > 4 || fs::exists(root)) return 2;
        if (mode != L"plan" && mode != L"directories" && mode != L"move" &&
            mode != L"copy" && mode != L"control") return 3;
        // Only operate in a newly created disposable fixture; never accept existing data.
        fs::create_directories(root / L"source");
        fs::create_directory(root / L"destination");
        const bool planning = mode == L"plan" || mode == L"directories";
        const unsigned count = mode == L"plan" ? 100'000 :
            mode == L"directories" ? 20'000 : mode == L"move" ? 3'000 : mode == L"copy" ? 8 : 0;
        std::string block(mode == L"copy" ? 1024 * 1024 : 4, 'v');
        for (unsigned index = 0; index < count; ++index) {
            const auto path = root / L"source" / std::to_wstring(index);
            if (mode == L"directories") fs::create_directory(path);
            else {
                std::ofstream stream(path, std::ios::binary);
                stream.exceptions(std::ios::badbit | std::ios::failbit);
                for (unsigned part = 0; part < (mode == L"copy" ? 32u : 1u); ++part)
                    stream.write(block.data(), static_cast<std::streamsize>(block.size()));
            }
        }
        CopyJob job{};
        job.sources = {root / L"source"};
        job.destination = root / L"destination";
        job.layout = DestinationLayout::ContentsOnly;
        job.operation = mode == L"move" ? FileOperation::Move : FileOperation::Copy;
        CopyPlan raw;
        if (!planning && mode != L"control") raw = JobPlanner{}.build(job);
        ExecutionControl control;
        std::uint64_t callbacks = 0;
        std::uint64_t reads = 0;
        FILETIME created{}, exited{}, kernel_before{}, user_before{}, kernel_after{}, user_after{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_before, &user_before);
        const auto start = Clock::now();
        double live_seconds = 0;
        double planning_seconds = 0;
        if (planning) {
            raw = JobPlanner{}.build(job);
            if ((mode == L"plan" && raw.files.size() != count) ||
                (mode == L"directories" && raw.directories.size() < count) || !raw.failures.empty()) return 4;
            const auto live_start = Clock::now();
            planning_seconds = std::chrono::duration<double>(live_start - start).count();
            LiveCopyPlan live(std::move(raw));
            live_seconds = std::chrono::duration<double>(Clock::now() - live_start).count();
        } else if (mode == L"control") {
            for (unsigned i = 0; i < 10'000'000; ++i)
                reads += control.directive() == ExecutionDirective::Run;
            if (reads != 10'000'000) return 5;
        } else {
            LiveCopyPlan live(std::move(raw));
            JobExecutionOptions options{};
            options.worker_count = workers;
            const auto result = JobExecutor{}.execute(live, control, options, [&](const JobProgress&) {
                ++callbacks;
                return JobDecision::Continue;
            });
            if (!result.success || result.outcomes.succeeded != count || result.outcomes.failed) return 6;
        }
        const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel_after, &user_after);
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb = sizeof(memory);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) return 7;
        if (!planning && mode != L"control") {
            for (unsigned index = 0; index < count; ++index) {
                const auto name = std::to_wstring(index);
                const auto destination = root / L"destination" / name;
                if (fs::file_size(destination) != (mode == L"copy" ? 32ull * 1024 * 1024 : 4ull)) return 8;
                std::ifstream stream(destination, std::ios::binary);
                std::string actual(block.size(), '\0');
                for (unsigned part = 0; part < (mode == L"copy" ? 32u : 1u); ++part) {
                    stream.read(actual.data(), static_cast<std::streamsize>(actual.size()));
                    if (!stream || actual != block) return 9;
                }
                if (fs::exists(root / L"source" / name) != (mode == L"copy")) return 10;
            }
        }
        const double cpu = static_cast<double>(ticks(kernel_after) - ticks(kernel_before) +
            ticks(user_after) - ticks(user_before)) / 10'000'000.0;
        std::wcout << std::fixed << std::setprecision(9)
            << L"{\"mode\":\"" << mode << L"\",\"workers\":" << workers
            << L",\"count\":" << count << L",\"seconds\":" << seconds
            << L",\"planning_seconds\":" << planning_seconds
            << L",\"live_plan_seconds\":" << live_seconds << L",\"cpu_seconds\":" << cpu
            << L",\"peak_working_set\":" << memory.PeakWorkingSetSize
            << L",\"callbacks\":" << callbacks << L",\"directive_reads\":" << reads << L"}\n";
        // root did not exist on entry and every file below it belongs to this invocation.
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 11;
    }
}
