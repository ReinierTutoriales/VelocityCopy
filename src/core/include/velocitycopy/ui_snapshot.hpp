#pragma once

#include "velocitycopy/job_executor.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace velocitycopy {

struct UiSnapshot {
    std::uint64_t sequence{};
    std::uint64_t total_bytes{};
    std::uint64_t transferred_bytes{};
    std::uint64_t total_files{};
    std::uint64_t completed_files{};
    double fraction{};
    double bytes_per_second{};
    double eta_seconds{};
    std::filesystem::path current_source;
    std::filesystem::path current_destination;
};

class ProgressPresenter final {
public:
    explicit ProgressPresenter(std::uint64_t emit_interval_ms = 100) noexcept;

    [[nodiscard]] std::optional<UiSnapshot> observe(
        const JobProgress& progress,
        std::uint64_t now_ms);

    void reset() noexcept;

private:
    std::uint64_t emit_interval_ms_{};
    std::uint64_t last_sample_ms_{};
    std::uint64_t last_emit_ms_{};
    std::uint64_t last_sample_bytes_{};
    std::uint64_t sequence_{};
    double smoothed_bytes_per_second_{};
    bool has_sample_{};
};

} // namespace velocitycopy
