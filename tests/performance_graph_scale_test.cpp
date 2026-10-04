#include "../src/ui/VelocityCopy.UI/PerformanceGraphScale.h"

#include <cmath>
#include <iostream>

namespace {
bool near(const double left, const double right) {
    return std::abs(left - right) <= 0.5;
}
}

int main() {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;

    const auto minimum = velocitycopy::ui::performance_scale(mib);
    if (!near(minimum.ceiling_bytes_per_second, mib) || !near(minimum.unit_bytes, mib)) {
        std::cerr << "1 MiB/s must keep a 1 MiB/s ceiling and MiB axis\n";
        return 1;
    }

    const auto crossover = velocitycopy::ui::performance_scale(1010.0 * mib);
    if (!near(crossover.ceiling_bytes_per_second, gib) || !near(crossover.unit_bytes, gib)) {
        std::cerr << "1010 MiB/s must normalize to a 1 GiB/s ceiling and GiB axis\n";
        return 2;
    }

    const auto exact_gib = velocitycopy::ui::performance_scale(1024.0 * mib);
    if (!near(exact_gib.ceiling_bytes_per_second, gib) || !near(exact_gib.unit_bytes, gib)) {
        std::cerr << "1024 MiB/s must use a 1 GiB/s ceiling and GiB axis\n";
        return 3;
    }

    const auto rounded = velocitycopy::ui::performance_scale(1.84 * gib);
    if (!near(rounded.ceiling_bytes_per_second, 2.0 * gib) || !near(rounded.unit_bytes, gib)) {
        std::cerr << "1.84 GiB/s must round to a 2 GiB/s ceiling and GiB axis\n";
        return 4;
    }

    const auto high = velocitycopy::ui::performance_scale(3.0 * gib);
    if (!near(high.ceiling_bytes_per_second, 5.0 * gib) || !near(high.unit_bytes, gib)) {
        std::cerr << "3 GiB/s must round to a 5 GiB/s ceiling and GiB axis\n";
        return 5;
    }

    return 0;
}
