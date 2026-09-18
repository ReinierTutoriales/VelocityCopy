#pragma once

#include "velocitycopy/destination_browser.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace velocitycopy {

struct DestinationNavigationResult {
    std::uint64_t generation{};
    bool available{};
    std::filesystem::path folder;
    std::vector<DestinationFolderEntry> children;
    DestinationCapacity capacity{};
};

using DestinationNavigationCallback = std::function<void(DestinationNavigationResult)>;

class DestinationNavigationWorker final {
public:
    DestinationNavigationWorker();
    ~DestinationNavigationWorker();

    DestinationNavigationWorker(const DestinationNavigationWorker&) = delete;
    DestinationNavigationWorker& operator=(const DestinationNavigationWorker&) = delete;

    [[nodiscard]] std::uint64_t navigate(
        std::filesystem::path folder,
        bool include_capacity,
        DestinationNavigationCallback callback);

    void cancel() noexcept;

private:
    struct Request {
        std::uint64_t generation{};
        std::filesystem::path folder;
        bool include_capacity{};
        DestinationNavigationCallback callback;
    };

    void run(std::stop_token stop_token) noexcept;

    DestinationBrowser browser_;
    std::mutex mutex_;
    std::condition_variable_any condition_;
    std::optional<Request> pending_;
    std::uint64_t latest_generation_{};
    std::jthread worker_;
};

} // namespace velocitycopy
