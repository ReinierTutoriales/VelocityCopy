#include "../src/ui/VelocityCopy.UI/TelemetryLayout.h"

#include <iostream>

namespace layout = velocitycopy::ui::layout;

namespace {
int fail(const int code, const char* message) {
    std::cerr << "telemetry layout contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    // Required window = ceil(content + chrome + margin): rounding never leaves the row at its exact limit.
    if (layout::required_normal_window_width(394.0, 16.0, 6.0) != 416.0) return fail(1, "required width");
    if (layout::required_normal_window_width(393.2, 16.0, 6.0) != 416.0) return fail(2, "required width must round up");

    // 380 x TextScale stays the final width whenever the reserved row already fits.
    if (layout::normal_target_width(475.0, 460.0, 2000.0) != 475.0) return fail(3, "125% must not grow");
    if (layout::normal_target_width(570.0, 510.0, 2000.0) != 570.0) return fail(4, "150% must not grow");

    // ...and the window grows only when it does not (100%).
    if (layout::normal_target_width(380.0, 416.0, 2000.0) != 416.0) return fail(5, "100% must grow to the floor");

    // The work-area cap still wins over both.
    if (layout::normal_target_width(380.0, 416.0, 400.0) != 400.0) return fail(6, "work-area cap must win");

    // The agreed ETA domain stops at 99 h 59 m; speeds stay below 100 GiB/s.
    if (layout::kEtaDomainMaxSeconds != 99.0 * 3600.0 + 59.0 * 60.0) return fail(7, "ETA domain cap");
    for (const double v : layout::kSpeedDomainBytesPerSecond) {
        if (v >= 100.0 * layout::kGiB) return fail(8, "speed sample outside the operating domain");
    }
    // Boundary samples must straddle the unit thresholds (1024 KiB/s and 1024.0 MiB/s cases).
    if (!(1048575.0 < layout::kMiB) || !(1073741823.0 < layout::kGiB)) return fail(9, "boundary samples");

    if (layout::digit_variants(L"1.3%").size() != 11) return fail(10, "digit variants");
    return 0;
}
