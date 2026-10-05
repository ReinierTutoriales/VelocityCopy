#include "../src/ui/VelocityCopy.UI/PerformanceGraphScale.h"

#include <cmath>
#include <deque>
#include <iostream>
#include <vector>

namespace {
bool near(const double left, const double right) {
    return std::abs(left - right) <= 0.5;
}
int fail(int code, const char* message) {
    std::cerr << message << '\n';
    return code;
}
std::vector<double> series(const std::size_t low_count, const std::size_t high_count,
                           const double low, const double high) {
    std::vector<double> values(low_count, low);
    values.insert(values.end(), high_count, high);
    return values;
}
}

int main() {
    using namespace velocitycopy::ui;
    constexpr double mib = kPerformanceMiB;
    constexpr double gib = kPerformanceGiB;
    constexpr double low = 105.0 * mib;
    constexpr double high = 2.0 * gib;

    // Nice binary-unit scale boundaries remain unchanged.
    if (!near(performance_scale(mib).ceiling_bytes_per_second, mib)) return fail(1, "minimum scale");
    if (!near(performance_scale(1010.0 * mib).ceiling_bytes_per_second, gib)) return fail(2, "MiB/GiB crossover");
    if (!near(performance_scale(1.84 * gib).ceiling_bytes_per_second, 2.0 * gib)) return fail(3, "nice rounding");

    // The policy constants are part of the contract: pin them literally (a loop bounded by the constant
    // itself would follow it if it were changed).
    if (kPerformanceScaleWarmupMinimumSamples != 4 || kPerformanceScaleStableWindowSamples != 20 ||
        kPerformanceScaleRiseConfirmSamples != 2 || kPerformanceScaleFallConfirmSamples != 6) {
        return fail(40, "scale policy constants changed (warm-up 4, stable window 20, hysteresis 2 up / 6 down)");
    }

    // Fewer than four samples preserve the truthful peak but do not infer a scale.
    for (std::size_t count = 1; count < 4; ++count) {
        auto values = series(count - 1, 1, low, high);
        const auto stats = performance_window_stats(values);
        if (stats.scale_reference_ready) return fail(4, "scale inferred before four samples");
        if (!near(stats.peak_bytes_per_second, high)) return fail(5, "startup peak lost");
    }

    // Warm-up uses the median, so one startup burst cannot own the axis.
    {
        const std::vector<double> values{high, low, low, low};
        const auto stats = performance_window_stats(values);
        if (!stats.scale_reference_ready || !near(stats.scale_reference_bytes_per_second, low))
            return fail(6, "warm-up median rejected startup burst");
        if (!near(stats.peak_bytes_per_second, high)) return fail(7, "warm-up peak not retained");
    }

    // A genuinely fast transfer establishes its real scale on the fourth sample.
    {
        const std::vector<double> values(4, high);
        const auto stats = performance_window_stats(values);
        PerformanceScaleState state{};
        const auto scale = update_performance_scale(state, stats.scale_reference_bytes_per_second);
        if (!stats.scale_reference_ready || !near(scale.ceiling_bytes_per_second, 2.0 * gib))
            return fail(8, "fast warm-up did not establish scale");
        if (performance_sample_clipped(high, scale)) return fail(9, "fast warm-up clipped sustained signal");
    }

    // P85 on 60 samples tolerates one through nine bursts, but the tenth crosses the boundary.
    for (std::size_t bursts = 1; bursts <= 9; ++bursts) {
        const auto stats = performance_window_stats(series(60 - bursts, bursts, low, high));
        if (!near(stats.scale_reference_bytes_per_second, low)) return fail(10, "P85 burst tolerance boundary too low");
        if (!near(stats.peak_bytes_per_second, high)) return fail(11, "stable-window peak lost");
    }
    {
        const auto stats = performance_window_stats(series(50, 10, low, high));
        if (!near(stats.scale_reference_bytes_per_second, high)) return fail(12, "P85 tenth-burst boundary wrong");
    }

    // Four through eight bursts remain classified as bursts in the stable window.
    for (std::size_t bursts = 4; bursts <= 8; ++bursts) {
        const auto stats = performance_window_stats(series(60 - bursts, bursts, low, high));
        if (!near(stats.scale_reference_bytes_per_second, low)) return fail(13, "4-8 bursts changed stable reference");
    }

    // Median -> P85 transition must not invent a scale jump.
    {
        std::vector<double> values(19, low);
        const auto warm = performance_window_stats(values);
        values.push_back(low);
        const auto stable = performance_window_stats(values);
        if (!near(warm.scale_reference_bytes_per_second, stable.scale_reference_bytes_per_second))
            return fail(14, "warm-up to P85 transition jumped");
    }

    // A real 105 MiB/s -> 2 GiB/s step must take control within 14 accepted samples.
    {
        std::deque<double> window(60, low);
        PerformanceScaleState state{};
        auto initial = performance_window_stats(std::vector<double>(window.begin(), window.end()));
        (void)update_performance_scale(state, initial.scale_reference_bytes_per_second);
        std::size_t accepted = 0;
        for (; accepted < 14 && current_performance_scale(state).ceiling_bytes_per_second < 2.0 * gib; ++accepted) {
            window.pop_front();
            window.push_back(high);
            const auto stats = performance_window_stats(std::vector<double>(window.begin(), window.end()));
            (void)update_performance_scale(state, stats.scale_reference_bytes_per_second);
        }
        if (current_performance_scale(state).ceiling_bytes_per_second < 2.0 * gib || accepted > 14)
            return fail(15, "real step adjusted too slowly");
    }

    // Once high samples leave the rolling window, six confirmed lower references lower the ceiling.
    {
        PerformanceScaleState state{};
        (void)update_performance_scale(state, high);
        for (std::size_t i = 0; i + 1 < kPerformanceScaleFallConfirmSamples; ++i) {
            const auto scale = update_performance_scale(state, low);
            if (scale.ceiling_bytes_per_second < 2.0 * gib) return fail(16, "fall hysteresis released early");
        }
        const auto scale = update_performance_scale(state, low);
        if (scale.ceiling_bytes_per_second >= 2.0 * gib) return fail(17, "fall hysteresis did not release");
    }

    // Rise requires two accepted samples.
    {
        PerformanceScaleState state{};
        (void)update_performance_scale(state, low);
        auto scale = update_performance_scale(state, high);
        if (scale.ceiling_bytes_per_second >= 2.0 * gib) return fail(18, "rise happened on first candidate");
        scale = update_performance_scale(state, high);
        if (scale.ceiling_bytes_per_second < 2.0 * gib) return fail(19, "rise was not confirmed on second candidate");
    }

    // Repainting only reads the state; it must not advance hysteresis.
    {
        PerformanceScaleState state{};
        (void)update_performance_scale(state, low);
        (void)update_performance_scale(state, high);
        const auto streak = state.rise_streak;
        for (int i = 0; i < 20; ++i) (void)current_performance_scale(state);
        if (state.rise_streak != streak || state.ceiling_bytes_per_second > 200.0 * mib)
            return fail(20, "scale reads advanced hysteresis");
    }

    // A clipped sample remains truthful through the independent peak.
    {
        const auto stats = performance_window_stats(series(59, 1, low, 5.0 * gib));
        const auto scale = performance_scale(stats.scale_reference_bytes_per_second);
        if (!near(stats.peak_bytes_per_second, 5.0 * gib)) return fail(21, "truthful peak lost");
        if (!performance_sample_clipped(stats.peak_bytes_per_second, scale)) return fail(22, "clipping state missing");
    }

    return 0;
}
