#pragma once
#include <cstdint>
#include <unordered_map>

namespace velocitycopy {
// UI thread only. Callers must marshal votes to the UI thread.
class EfficiencyCoordinator {
public:
    bool update(std::uint64_t window_id, bool eligible);
    bool remove(std::uint64_t window_id);
    bool set_shutting_down(bool value);
    [[nodiscard]] bool desired() const noexcept;
private:
    std::unordered_map<std::uint64_t, bool> votes_;
    bool shutting_down_{};
};
} // namespace velocitycopy
