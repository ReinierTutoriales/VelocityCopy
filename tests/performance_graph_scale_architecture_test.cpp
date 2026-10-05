#include "architecture_support.hpp"

#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(int code, const char* message) {
    std::cerr << "performance graph architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto window = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto header = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto xaml = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto scale = read_source(root / "src/ui/VelocityCopy.UI/PerformanceGraphScale.h");
    if (window.empty() || header.empty() || xaml.empty() || scale.empty()) return fail(1, "required source missing");

    if (!contains(scale, "0.85 * static_cast<double>(ordered.size() - 1)") ||
        !contains(scale, "ordered.size() < kPerformanceScaleStableWindowSamples") ||
        !contains(scale, "median_of_sorted(ordered)") ||
        !contains(scale, "ordered.size() < kPerformanceScaleWarmupMinimumSamples")) {
        return fail(2, "scale helper must use median warm-up and P85 stable window");
    }

    const auto observe = body_of(window, "void MainWindow::ObservePerformanceSample(");
    if (!contains(observe, "performance_window_stats(samples)") ||
        !contains(observe, "stats.scale_reference_ready") ||
        !contains(observe, "update_performance_scale(")) {
        return fail(3, "accepted samples must own scale hysteresis");
    }

    const auto render = body_of(window, "void MainWindow::UpdatePerformanceGraph()");
    if (!contains(render, "current_performance_scale(performance_scale_state_)") ||
        !contains(render, "(std::clamp)(sample / scale_max, 0.0, 1.0)")) {
        return fail(4, "renderer must read stable scale and clamp rendered samples");
    }
    if (contains(render, "update_performance_scale(") ||
        contains(render, "performance_scale(peak)") ||
        contains(render, "for (const double sample : performance_speed_samples_) peak")) {
        return fail(5, "paint must not advance hysteresis or return to max-of-window scaling");
    }

    if (!contains(header, "PerformanceScaleState performance_scale_state_") ||
        contains(header, "performance_window_peak_") ||
        contains(header, "performance_graph_clipped_")) {
        return fail(6, "stable scale must be owned; unused peak/clipping write-only fields must stay removed");
    }

    if (contains(xaml, "PerformancePeakLabel") ||
        contains(xaml, "PerformancePeakSpeedText") ||
        contains(xaml, "PerformancePeakClippedMarker") ||
        contains(xaml, "Text=\"↑\"")) {
        return fail(7, "unverified persistent peak header UI must stay deferred");
    }

    return 0;
}
