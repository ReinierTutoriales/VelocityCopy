#pragma once

#include <algorithm>
#include <cmath>

namespace velocitycopy::ui {

struct PerformanceScale {
    double ceiling_bytes_per_second;
    double unit_bytes;
};

inline PerformanceScale performance_scale(const double peak_bytes_per_second) {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    const double peak = (std::max)(mib, peak_bytes_per_second);
    double unit = peak >= gib ? gib : mib;
    const double peak_in_unit = peak / unit;
    const double exponent = std::floor(std::log10(peak_in_unit));
    const double magnitude = std::pow(10.0, exponent);
    const double normalized_peak = peak_in_unit / magnitude;
    const double nice_factor =
        normalized_peak <= 1.0 ? 1.0 :
        normalized_peak <= 2.0 ? 2.0 :
        normalized_peak <= 5.0 ? 5.0 : 10.0;
    double ceiling_in_unit = nice_factor * magnitude;
    if (unit == mib && ceiling_in_unit >= 1024.0) {
        unit = gib;
        ceiling_in_unit = 1.0;
    }
    return {ceiling_in_unit * unit, unit};
}

} // namespace velocitycopy::ui
