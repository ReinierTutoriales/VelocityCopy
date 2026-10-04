#pragma once

// Pure helpers for the telemetry geometry probe (diagnostic builds only).
// No WinRT/XAML dependency so the logic is unit-testable off the UI thread.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "TelemetryLayout.h"

namespace velocitycopy::ui::probe {

// Column order of one log line. Keep in sync with MainWindow::LogTelemetryGeometry().
inline constexpr std::wstring_view kLogColumns =
    L"build | mode | hwnd_px | factor | raster | speed_max | pct_max | eta_max | reserves | declared_gaps | "
    L"live_gaps | tele | cluster | details | avail | required | holgura | x_pause | x_stop | x_options | "
    L"x_details | details_right | paused | eta_unbounded | validation";

using layout::digit_variants;

// Keeps a field on one log line and free of the column separator.
inline std::wstring sanitize_field(const std::wstring_view value) {
    std::wstring out(value);
    for (auto& c : out) {
        if (c == L'|' || c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
    }
    return out;
}

inline std::wstring join_fields(const std::vector<std::wstring>& fields) {
    std::wstring line;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) line += L" | ";
        line += sanitize_field(fields[i]);
    }
    return line;
}

// True when the ETA text shows a hour count wider than two digits ("100 h 00 m").
inline bool eta_hours_exceed_two_digits(const std::wstring_view text) {
    const auto marker = text.find(L" h ");
    if (marker == std::wstring_view::npos) return false;
    std::size_t digits = 0;
    for (std::size_t i = 0; i < marker; ++i) {
        if (text[i] >= L'0' && text[i] <= L'9') ++digits;
    }
    return digits > 2;
}

inline bool widths_match(const double a, const double b, const double tolerance = 0.5) {
    const double diff = a > b ? a - b : b - a;
    return diff <= tolerance;
}

} // namespace velocitycopy::ui::probe
