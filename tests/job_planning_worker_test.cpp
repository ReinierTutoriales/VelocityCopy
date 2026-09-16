#include "velocitycopy/job_planning_worker.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
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

    fs::remove_all(root, ec);
    return 0;
}
