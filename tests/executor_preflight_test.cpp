#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include <windows.h>
#include <filesystem>
#include <future>
#include <chrono>
#include <cstdlib>
#include <fstream>

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;
    using namespace std::chrono_literals;
    const auto root = fs::temp_directory_path() / L"VelocityCopyPreflightTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"source" / L"empty" / L"deep");
    auto raw_plan = [&] {
        CopyPlan raw{};
        raw.operation = FileOperation::Move;
        raw.source_roots = {root / L"source"};
        raw.destination_root = root / L"destination";
        raw.directories = {{raw.destination_root}};
        return raw;
    };
    for (bool cancel : {false, true}) {
        LiveCopyPlan plan(raw_plan());
        ExecutionControl control;
        if (cancel) control.request_cancel(); else control.request_stop();
        const auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {});
        if (result.success || result.cancelled != cancel || result.stopped == cancel ||
            fs::exists(root / L"destination") || !fs::exists(root / L"source" / L"empty")) return 1;
    }
    {
        LiveCopyPlan plan(raw_plan());
        ExecutionControl control;
        control.request_pause();
        auto task = std::async(std::launch::async, [&] {
            return JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {});
        });
        const bool waited = task.wait_for(100ms) == std::future_status::timeout;
        const bool unchanged = !fs::exists(root / L"destination");
        control.request_cancel();
        if (!waited || !unchanged || !task.get().cancelled) return 2;
    }
    // Normal cleanup removes empty descendants but preserves newly present files.
    {
        LiveCopyPlan plan(raw_plan());
        ExecutionControl control;
        if (!JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {}).success ||
            fs::exists(root / L"source")) return 3;
    }
    fs::create_directories(root / L"source" / L"empty");
    { std::ofstream stream(root / L"source" / L"keep.txt"); stream << "new file"; }
    {
        LiveCopyPlan plan(raw_plan());
        ExecutionControl control;
        if (!JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {}).success ||
            !fs::exists(root / L"source" / L"keep.txt") || fs::exists(root / L"source" / L"empty")) return 6;
    }
    fs::remove_all(root / L"source");
    // Junctions must be rejected, never traversed into an unrelated tree.
    fs::create_directories(root / L"external" / L"keep");
    const auto junction = root / L"source";
    const auto command = L"cmd /c mklink /J \"" + junction.wstring() + L"\" \"" +
        (root / L"external").wstring() + L"\" >nul";
    if (_wsystem(command.c_str()) != 0) return 4;
    {
        LiveCopyPlan plan(raw_plan());
        ExecutionControl control;
        if (JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {}).success ||
            !fs::exists(root / L"external" / L"keep")) return 5;
    }
    fs::remove(junction, ec);
    fs::create_directories(junction);
    const auto nested = junction / L"nested";
    const auto nested_command = L"cmd /c mklink /J \"" + nested.wstring() + L"\" \"" +
        (root / L"external").wstring() + L"\" >nul";
    if (_wsystem(nested_command.c_str()) != 0) return 7;
    {
        LiveCopyPlan plan(raw_plan());
        ExecutionControl control;
        if (JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {}).success ||
            !fs::exists(root / L"external" / L"keep")) return 8;
    }
    fs::remove(nested, ec);
    fs::remove(junction, ec);
    fs::remove_all(root, ec);
    return 0;
}
