#pragma once

// Pure layout rules for the bottom command row of the normal transfer view.
// No WinRT/XAML dependency: the rules are unit-testable off the UI thread.
//
// The normal window width is
//     normal_target_width = min(max(380 * TextScale, required_window), work_area_cap)
// so 380 epx stays the scaled minimum (it is a floor, not a final width) and the
// window only grows when the reserved row cannot fit.

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace velocitycopy::ui::layout {

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

inline constexpr double kKiB = 1024.0;
inline constexpr double kMiB = 1024.0 * kKiB;
inline constexpr double kGiB = 1024.0 * kMiB;

// Operating domain reserved for the Speed / Percent / ETA cells. Values outside it
// are trimmed with an ellipsis instead of widening the window or moving the actions.
// Every sample is rendered through the real formatters (FormatSpeed / FormatProgressPercent /
// FormatEta), never as a literal string.
//
// Speed: em dash, the KiB/MiB rounding boundaries (1024 KiB/s, 1024.0 MiB/s), 999.9 MiB/s
// and 99.99 GiB/s. Anything >= 100 GiB/s is outside the domain.
inline constexpr double kSpeedDomainBytesPerSecond[] = {
    0.0, 1023.0 * kKiB, 1048575.0, 1073741823.0, 999.9 * kMiB, 99.99 * kGiB};
// Percent: the whole format is bounded ("<0.1%" .. "100%").
inline constexpr double kPercentDomainFractions[] = {0.0, 0.0005, 0.013, 0.099, 0.5, 0.999, 1.0};
// ETA: FormatEta() is unbounded in hours. The agreed operating domain stops at 99 h 59 m.
inline constexpr double kEtaDomainMaxSeconds = 99.0 * 3600.0 + 59.0 * 60.0;
inline constexpr double kEtaDomainSeconds[] = {0.0, 59.0, 3599.0, 3600.0, kEtaDomainMaxSeconds};

// Probe string for the text engine. The reserve cache is keyed on what the engine MEASURES for
// this string, not on what the OS reports: after a live Text Size change the engine can still be
// measuring with the previous scale when the change notification arrives.
inline constexpr const wchar_t* kReserveCanaryText = L"8888.8 MiB/s";

inline bool canary_changed(const double applied, const double current) {
    return std::abs(applied - current) > 0.25;
}

// Window width (epx) needed so the bottom row fits its reserved cells:
// content of the row at unconstrained width + horizontal chrome around it + a safety margin
// that absorbs layout rounding and rasterization differences. Rounded up to a whole epx.
inline double required_normal_window_width(
    const double required_content, const double chrome, const double fit_margin) {
    return std::ceil(required_content + chrome + fit_margin);
}

// scaled_base: NormalWindowMinWidth * TextScaleFactor (the previous final width).
inline double normal_target_width(
    const double scaled_base, const double required_window, const double work_area_cap) {
    return (std::min)((std::max)(scaled_base, required_window), work_area_cap);
}

} // namespace velocitycopy::ui::layout
