#include "velocitycopy/efficiency_coordinator.hpp"
#include <algorithm>

namespace velocitycopy {
bool EfficiencyCoordinator::update(const std::uint64_t window_id, const bool eligible) {
    votes_[window_id] = eligible;
    return desired();
}
bool EfficiencyCoordinator::remove(const std::uint64_t window_id) {
    votes_.erase(window_id);
    return desired();
}
bool EfficiencyCoordinator::set_shutting_down(const bool value) {
    shutting_down_ = value;
    return desired();
}
bool EfficiencyCoordinator::desired() const noexcept {
    return !shutting_down_ && !votes_.empty() &&
        std::all_of(votes_.begin(), votes_.end(), [](const auto& vote) { return vote.second; });
}
} // namespace velocitycopy
