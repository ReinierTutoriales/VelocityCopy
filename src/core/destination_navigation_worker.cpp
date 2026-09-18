#include "velocitycopy/destination_navigation_worker.hpp"

#include <utility>

namespace velocitycopy {

DestinationNavigationWorker::DestinationNavigationWorker()
    : worker_([this](std::stop_token token) { run(token); }) {
}

DestinationNavigationWorker::~DestinationNavigationWorker() {
    worker_.request_stop();
    condition_.notify_all();
}

std::uint64_t DestinationNavigationWorker::navigate(
    std::filesystem::path folder,
    const bool include_capacity,
    DestinationNavigationCallback callback) {
    std::lock_guard lock(mutex_);
    const auto generation = ++latest_generation_;
    pending_ = Request{
        generation,
        std::move(folder),
        include_capacity,
        std::move(callback),
    };
    condition_.notify_one();
    return generation;
}

void DestinationNavigationWorker::cancel() noexcept {
    std::lock_guard lock(mutex_);
    ++latest_generation_;
    pending_.reset();
}

void DestinationNavigationWorker::run(const std::stop_token stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        Request request{};
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, stop_token, [this] { return pending_.has_value(); });
            if (stop_token.stop_requested()) {
                return;
            }
            request = std::move(*pending_);
            pending_.reset();
        }

        DestinationNavigationResult result{};
        result.generation = request.generation;
        result.folder = request.folder;
        auto listing = browser_.list_children(request.folder);
        result.available = listing.available;
        result.children = std::move(listing.children);
        if (request.include_capacity) {
            result.capacity = browser_.capacity(request.folder);
        }

        {
            std::lock_guard lock(mutex_);
            if (request.generation != latest_generation_) {
                continue;
            }
        }

        if (request.callback) {
            try {
                request.callback(std::move(result));
            } catch (...) {
                // UI-facing callbacks must never terminate the background worker.
            }
        }
    }
}

} // namespace velocitycopy
