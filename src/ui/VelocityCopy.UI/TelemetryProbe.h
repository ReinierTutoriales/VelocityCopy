#pragma once

// Pure helpers for the telemetry geometry probe (diagnostic builds only).
// No WinRT/XAML dependency so the logic is unit-testable off the UI thread.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace velocitycopy::ui::probe {

// Column order of one log line. Keep in sync with MainWindow::LogTelemetryGeometry().
inline constexpr std::wstring_view kLogColumns =
    L"build | mode | hwnd_px | factor | raster | speed_max | pct_max | eta_max | declared_gaps | live_gaps | "
    L"tele_worst | cluster | details_worst | util | holgura | paused | eta_unbounded | validation";

// The sample itself plus the sample with every digit replaced by 0..9.
inline std::vector<std::wstring> digit_variants(const std::wstring_view text) {
    std::vector<std::wstring> out;
    out.emplace_back(text);
    bool has_digit = false;
    for (const wchar_t c : text) has_digit = has_digit || (c >= L'0' && c <= L'9');
    if (!has_digit) return out;
    for (wchar_t digit = L'0'; digit <= L'9'; ++digit) {
        std::wstring variant(text);
        for (auto& c : variant) {
            if (c >= L'0' && c <= L'9') c = digit;
        }
        out.push_back(std::move(variant));
    }
    return out;
}

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
