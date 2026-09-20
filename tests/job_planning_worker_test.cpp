#include "velocitycopy/job_planning_worker.hpp"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <tuple>
#include <vector>

using namespace std::chrono_literals;

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopyPlanningWorkerTest";
    const auto source_a = root / L"a";
    const auto source_b = root / L"b";
    const auto destination = root / L"destination";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(source_a, ec);
    fs::create_directories(source_b, ec);
    if (ec) {
        return 1;
    }

    {
        std::ofstream(source_a / L"a.txt") << "a";
        std::ofstream(source_b / L"b.txt") << "b";
    }

    CopyJob first{};
    first.id = 10;
    first.sources = {source_a / L"a.txt"};
    first.destination = destination;
    first.layout = DestinationLayout::ContentsOnly;

    CopyJob second{};
    second.id = 11;
    second.sources = {source_b / L"b.txt"};
    second.destination = destination;
    second.layout = DestinationLayout::ContentsOnly;

    JobPlanningWorker worker;
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::uint64_t> request_order;
    std::vector<std::uint64_t> job_order;
    bool failed = false;

    auto callback = [&](JobPlanningResult result) {
        {
            std::lock_guard lock(mutex);
            if (!result.plan || result.error_code != 0) {
                failed = true;
            }
            request_order.push_back(result.request_id);
            job_order.push_back(result.job.id);
        }
        condition.notify_one();
    };

    const auto first_request = worker.enqueue(first, callback);
    const auto second_request = worker.enqueue(second, callback);

    {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(lock, 5s, [&] { return request_order.size() == 2; })) {
            fs::remove_all(root, ec);
            return 2;
        }
    }

    if (failed || request_order.size() != 2 || job_order.size() != 2 ||
        request_order[0] != first_request || request_order[1] != second_request ||
        job_order[0] != first.id || job_order[1] != second.id) {
        fs::remove_all(root, ec);
        return 3;
    }

    // Build enough metadata work to ensure the first request is actively
    // enumerating when cancellation is issued. The second request remains
    // queued. Both must complete their callbacks as explicit cancellations so
    // callers can release planning reservations without leaks.
    const auto slow_source = root / L"slow";
    fs::create_directories(slow_source, ec);
    if (ec) {
        fs::remove_all(root, ec);
        return 4;
    }
    for (int index = 0; index < 4096; ++index) {
        std::ofstream(slow_source / (L"f" + std::to_wstring(index) + L".bin"), std::ios::binary);
    }

    CopyJob active{};
    active.id = 20;
    active.sources = {slow_source};
    active.destination = destination / L"cancelled";
    active.layout = DestinationLayout::PreserveSourceFolder;

    CopyJob queued = active;
    queued.id = 21;

    std::vector<std::tuple<std::uint64_t, std::int32_t, bool>> cancelled_results;
    auto cancellation_callback = [&](JobPlanningResult result) {
        {
            std::lock_guard lock(mutex);
            cancelled_results.emplace_back(result.job.id, result.error_code, result.plan.has_value());
        }
        condition.notify_all();
    };

    (void)worker.enqueue(active, cancellation_callback);
    (void)worker.enqueue(queued, cancellation_callback);
    Sleep(10);
    worker.cancel_pending();

    {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(lock, 10s, [&] { return cancelled_results.size() == 2; })) {
            fs::remove_all(root, ec);
            return 5;
        }
    }

    const auto aborted = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
    bool active_cancelled = false;
    bool queued_cancelled = false;
    for (const auto& [job_id, error_code, has_plan] : cancelled_results) {
        if (error_code != aborted || has_plan) {
            fs::remove_all(root, ec);
            return 6;
        }
        active_cancelled = active_cancelled || job_id == active.id;
        queued_cancelled = queued_cancelled || job_id == queued.id;
    }
    if (!active_cancelled || !queued_cancelled) {
        fs::remove_all(root, ec);
        return 7;
    }

    fs::remove_all(root, ec);
    return 0;
}
