#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace velocitycopy::ui {

inline constexpr double kPerformanceMiB = 1024.0 * 1024.0;
inline constexpr double kPerformanceGiB = 1024.0 * 1024.0 * 1024.0;
inline constexpr std::size_t kPerformanceScaleWarmupMinimumSamples = 4;
inline constexpr std::size_t kPerformanceScaleStableWindowSamples = 20;
inline constexpr std::size_t kPerformanceScaleRiseConfirmSamples = 2;
inline constexpr std::size_t kPerformanceScaleFallConfirmSamples = 6;

struct PerformanceScale {
    double ceiling_bytes_per_second;
    double unit_bytes;
};

struct PerformanceWindowStats {
    double scale_reference_bytes_per_second{};
    double peak_bytes_per_second{};
    std::size_t sample_count{};
    bool scale_reference_ready{};
};

struct PerformanceScaleState {
    double ceiling_bytes_per_second{};
    std::size_t rise_streak{};
    std::size_t fall_streak{};
};

inline PerformanceScale performance_scale(const double reference_bytes_per_second) {
    const double reference = (std::max)(kPerformanceMiB, reference_bytes_per_second);
    double unit = reference >= kPerformanceGiB ? kPerformanceGiB : kPerformanceMiB;
    const double reference_in_unit = reference / unit;
    const double exponent = std::floor(std::log10(reference_in_unit));
    const double magnitude = std::pow(10.0, exponent);
    const double normalized_reference = reference_in_unit / magnitude;
    const double nice_factor =
        normalized_reference <= 1.0 ? 1.0 :
        normalized_reference <= 2.0 ? 2.0 :
        normalized_reference <= 5.0 ? 5.0 : 10.0;
    double ceiling_in_unit = nice_factor * magnitude;
    if (unit == kPerformanceMiB && ceiling_in_unit >= 1024.0) {
        unit = kPerformanceGiB;
        ceiling_in_unit = 1.0;
    }
    return {ceiling_in_unit * unit, unit};
}

inline double median_of_sorted(const std::vector<double>& ordered) {
    const auto count = ordered.size();
    if (count == 0) return 0.0;
    const auto middle = count / 2;
    if ((count % 2) != 0) return ordered[middle];
    return (ordered[middle - 1] + ordered[middle]) / 2.0;
}

// Before 20 samples, the median gives a useful scale quickly without letting a startup
// burst dominate it. Once the window is established, P85 tolerates the burst density
// observed in real transfers while the independent peak retains every real maximum.
inline PerformanceWindowStats performance_window_stats(const std::vector<double>& samples) {
    PerformanceWindowStats result{};
    result.sample_count = samples.size();
    if (samples.empty()) return result;

    std::vector<double> ordered;
    ordered.reserve(samples.size());
    for (const double sample : samples) {
        const double safe = std::isfinite(sample) && sample > 0.0 ? sample : 0.0;
        ordered.push_back(safe);
        result.peak_bytes_per_second = (std::max)(result.peak_bytes_per_second, safe);
    }
    std::sort(ordered.begin(), ordered.end());

    if (ordered.size() < kPerformanceScaleWarmupMinimumSamples) {
        return result;
    }

    result.scale_reference_ready = true;
    if (ordered.size() < kPerformanceScaleStableWindowSamples) {
        result.scale_reference_bytes_per_second = median_of_sorted(ordered);
        return result;
    }

    const auto index = static_cast<std::size_t>(
        std::floor(0.85 * static_cast<double>(ordered.size() - 1)));
    result.scale_reference_bytes_per_second = ordered[index];
    return result;
}

// Hysteresis advances once per accepted performance sample, never once per paint.
// Two consecutive higher candidates confirm a rise; six lower candidates confirm decay.
inline PerformanceScale update_performance_scale(
    PerformanceScaleState& state,
    const double reference_bytes_per_second) {
    const auto candidate = performance_scale(reference_bytes_per_second);
    if (state.ceiling_bytes_per_second <= 0.0) {
        state.ceiling_bytes_per_second = candidate.ceiling_bytes_per_second;
        state.rise_streak = 0;
        state.fall_streak = 0;
    } else if (candidate.ceiling_bytes_per_second > state.ceiling_bytes_per_second) {
        state.fall_streak = 0;
        if (++state.rise_streak >= kPerformanceScaleRiseConfirmSamples) {
            state.ceiling_bytes_per_second = candidate.ceiling_bytes_per_second;
            state.rise_streak = 0;
        }
    } else if (candidate.ceiling_bytes_per_second < state.ceiling_bytes_per_second) {
        state.rise_streak = 0;
        if (++state.fall_streak >= kPerformanceScaleFallConfirmSamples) {
            state.ceiling_bytes_per_second = candidate.ceiling_bytes_per_second;
            state.fall_streak = 0;
        }
    } else {
        state.rise_streak = 0;
        state.fall_streak = 0;
    }

    const double ceiling = (std::max)(kPerformanceMiB, state.ceiling_bytes_per_second);
    return {ceiling, ceiling >= kPerformanceGiB ? kPerformanceGiB : kPerformanceMiB};
}

inline PerformanceScale current_performance_scale(const PerformanceScaleState& state) {
    const double ceiling = state.ceiling_bytes_per_second > 0.0
        ? state.ceiling_bytes_per_second
        : kPerformanceMiB;
    return {ceiling, ceiling >= kPerformanceGiB ? kPerformanceGiB : kPerformanceMiB};
}

inline bool performance_sample_clipped(const double sample, const PerformanceScale& scale) {
    return std::isfinite(sample) && sample > scale.ceiling_bytes_per_second;
}

} // namespace velocitycopy::ui
