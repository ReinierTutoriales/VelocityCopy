#include "../src/ui/VelocityCopy.UI/TelemetryProbe.h"

#include <format>
#include <iostream>
#include <string>

namespace probe = velocitycopy::ui::probe;

namespace {
int fail(const int code, const char* message) {
    std::cerr << "telemetry probe contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    // Digit variants: original + ten replacements; non-digit text is left alone.
    const auto eta = probe::digit_variants(L"59 m 48 s");
    if (eta.size() != 11 || eta[0] != L"59 m 48 s" || eta[1] != L"00 m 00 s" || eta[10] != L"99 m 99 s")
        return fail(1, "digit variants wrong");
    if (probe::digit_variants(L"\u2014").size() != 1) return fail(2, "text without digits must not expand");

    // Sample values chosen for the boundary cases must really straddle the unit thresholds.
    constexpr double kib = 1024.0;
    constexpr double mib = kib * 1024.0;
    constexpr double gib = mib * 1024.0;
    if (!(1048575.0 < mib) || std::format(L"{:.0f}", 1048575.0 / kib) != L"1024")
        return fail(3, "1048575 B/s must render as 1024 KiB/s");
    if (!(1073741823.0 < gib) || std::format(L"{:.1f}", 1073741823.0 / mib) != L"1024.0")
        return fail(4, "1073741823 B/s must render as 1024.0 MiB/s");
    if (std::format(L"{:.2f}", 10.0 * gib / gib) != L"10.00") return fail(5, "10 GiB/s sample");

    // Hour count wider than two digits is what marks the ETA as unbounded.
    if (!probe::eta_hours_exceed_two_digits(L"100 h 00 m")) return fail(6, "100 h must be unbounded");
    if (probe::eta_hours_exceed_two_digits(L"99 h 59 m")) return fail(7, "99 h is within the provisional cap");
    if (probe::eta_hours_exceed_two_digits(L"5 m 48 s")) return fail(8, "no hour part");

    // Log lines stay on one line and never contain the column separator inside a field.
    const auto line = probe::join_fields({L"a|b", L"c\nd"});
    if (line != L"a b | c d") return fail(9, "sanitize/join wrong");

    if (!probe::widths_match(10.0, 10.4) || probe::widths_match(10.0, 10.6)) return fail(10, "width tolerance");
    return 0;
}
