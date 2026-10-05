#pragma once
#include "velocitycopy/ui_snapshot.hpp"
#include <mutex>
#include <optional>
#include <utility>

namespace velocitycopy::ui {
// One pending dispatcher callback owns the latest snapshot, even when the UI
// thread is busy. Each execution session owns a separate mailbox.
class UiSnapshotMailbox final {
public:
    bool publish(UiSnapshot value) {
        std::lock_guard lock(mutex_);
        if (has_sequence_ && value.sequence <= highest_sequence_) return false;
        highest_sequence_ = value.sequence;
        has_sequence_ = true;
        latest_ = std::move(value);
        return !std::exchange(queued_, true);
    }
    std::optional<UiSnapshot> consume() {
        std::lock_guard lock(mutex_);
        queued_ = false;
        return std::exchange(latest_, std::nullopt);
    }
    void discard() {
        std::lock_guard lock(mutex_);
        queued_ = false;
        latest_.reset();
    }
private:
    std::mutex mutex_;
    std::optional<UiSnapshot> latest_;
    std::uint64_t highest_sequence_{};
    bool has_sequence_{};
    bool queued_{};
};
}
