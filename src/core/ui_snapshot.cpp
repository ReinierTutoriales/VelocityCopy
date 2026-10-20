#include "velocitycopy/ui_snapshot.hpp"

#include <algorithm>
#include <cmath>

namespace velocitycopy {

bool can_skip_current_file(
    const bool has_execution,
    const std::uint64_t current_file_id,
    const bool current_file_skippable,
    const bool paused,
    const bool stopped,
    const bool conflict,
    const bool stop_requested) noexcept {
    return has_execution && current_file_id != 0 && current_file_skippable &&
        !paused && !stopped && !conflict && !stop_requested;
}

ProgressPresenter::ProgressPresenter(const std::uint64_t emit_interval_ms) noexcept
    : emit_interval_ms_(std::max<std::uint64_t>(16, emit_interval_ms)) {}

std::optional<UiSnapshot> ProgressPresenter::observe(
    const JobProgress& progress,
    const std::uint64_t now_ms) {
    if (!has_sample_) {
        has_sample_ = true;
        first_sample_ms_ = now_ms;
        last_sample_ms_ = now_ms;
        last_emit_ms_ = now_ms;
        last_sample_bytes_ = progress.bytes_written_physical;
    } else if (now_ms > last_sample_ms_) {
        const auto elapsed_ms = now_ms - last_sample_ms_;
        // Speed is physical I/O only. Logical progress jumps when an item is
        // skipped or fails and must not register as throughput.
        const auto delta_bytes = progress.bytes_written_physical >= last_sample_bytes_
            ? progress.bytes_written_physical - last_sample_bytes_
            : 0;
        const double instant_rate = elapsed_ms == 0
            ? 0.0
            : (static_cast<double>(delta_bytes) * 1000.0) / static_cast<double>(elapsed_ms);

        // A one-second time constant gives the same response at any callback
        // frequency. Zero-I/O intervals decay stale throughput as well.
        const double alpha = -std::expm1(-static_cast<double>(elapsed_ms) / 1000.0);
        smoothed_bytes_per_second_ += alpha * (instant_rate - smoothed_bytes_per_second_);

        last_sample_ms_ = now_ms;
        last_sample_bytes_ = progress.bytes_written_physical;
    }

    const bool finished = progress.total_bytes == 0
        ? progress.total_files != 0 && progress.completed_files >= progress.total_files
        : progress.transferred_bytes >= progress.total_bytes;
    const bool due = now_ms >= last_emit_ms_ && (now_ms - last_emit_ms_) >= emit_interval_ms_;

    if (!finished && !due && sequence_ != 0) {
        return std::nullopt;
    }

    last_emit_ms_ = now_ms;

    UiSnapshot snapshot{};
    snapshot.sequence = ++sequence_;
    snapshot.total_bytes = progress.total_bytes;
    snapshot.transferred_bytes = std::min(progress.transferred_bytes, progress.total_bytes);
    snapshot.total_files = progress.total_files;
    snapshot.completed_files = std::min(progress.completed_files, progress.total_files);
    snapshot.current_file_id = progress.current_file_id;
    snapshot.current_file_skippable = progress.current_file_skippable;
    snapshot.bytes_per_second = smoothed_bytes_per_second_;
    snapshot.current_source = progress.current_source;
    snapshot.current_destination = progress.current_destination;

    if (progress.total_bytes != 0) {
        snapshot.fraction = std::clamp(
            static_cast<double>(snapshot.transferred_bytes) / static_cast<double>(progress.total_bytes),
            0.0,
            1.0);

        const bool eta_ready = now_ms >= first_sample_ms_ && now_ms - first_sample_ms_ >= 3'000;
        if (eta_ready && smoothed_bytes_per_second_ > 1.0 && snapshot.transferred_bytes < progress.total_bytes) {
            const auto remaining = progress.total_bytes - snapshot.transferred_bytes;
            snapshot.eta_seconds = static_cast<double>(remaining) / smoothed_bytes_per_second_;
        }
    } else if (progress.total_files != 0) {
        snapshot.fraction = std::clamp(
            static_cast<double>(snapshot.completed_files) / static_cast<double>(progress.total_files),
            0.0,
            1.0);
    }

    return snapshot;
}

void ProgressPresenter::reset() noexcept {
    first_sample_ms_ = 0;
    last_sample_ms_ = 0;
    last_emit_ms_ = 0;
    last_sample_bytes_ = 0;
    sequence_ = 0;
    smoothed_bytes_per_second_ = 0.0;
    has_sample_ = false;
}

} // namespace velocitycopy
