#include "velocitycopy/destination_navigation_worker.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyNavigationWorkerTest";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / L"Alpha", ec);
    fs::create_directories(base / L"Beta", ec);
    if (ec) {
        return 1;
    }

    velocitycopy::DestinationNavigationWorker worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<velocitycopy::DestinationNavigationResult> received;

    const auto generation = worker.navigate(base, false, [&](auto result) {
        {
            std::lock_guard lock(mutex);
            received = std::move(result);
        }
        cv.notify_one();
    });

    {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, 5s, [&] { return received.has_value(); })) {
            fs::remove_all(base, ec);
            return 2;
        }
    }

    if (received->generation != generation || received->folder != base || received->children.size() != 2) {
        fs::remove_all(base, ec);
        return 3;
    }

    worker.cancel();
    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy destination navigation worker test passed.\n";
    return 0;
}
