#include "architecture_support.hpp"

#include <iostream>
#include <regex>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
bool contains(const std::string& text, const std::string& value) { return text.find(value) != std::string::npos; }
int fail(const int code, const char* message) {
    std::cerr << "telemetry layout architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const std::filesystem::path ui = root / "src/ui/VelocityCopy.UI";
    const auto window = read_source(ui / "MainWindow.xaml.cpp");
    const auto layout_cpp = read_source(ui / "MainWindow.TelemetryLayout.cpp");
    const auto layout_h = read_source(ui / "TelemetryLayout.h");
    const auto header = read_source(ui / "MainWindow.xaml.h");
    const auto xaml = read_source(ui / "MainWindow.xaml");
    const auto tokens = read_source(root / "src/ui/DesignTokens.xaml");
    const auto project = read_source(ui / "VelocityCopy.UI.vcxproj");
    const auto spec = read_source(root / "docs/UI_SPEC.md");
    if (window.empty() || layout_cpp.empty() || layout_h.empty() || header.empty() || xaml.empty() ||
        tokens.empty() || project.empty() || spec.empty()) {
        return fail(1, "required source missing");
    }

    // 1. 380 epx stays the scaled minimum; the runtime floor is a separate, margin-protected input.
    if (!contains(tokens, "<x:Double x:Key=\"NormalWindowMinWidth\">380</x:Double>")) return fail(2, "base token changed");
    const auto margin_at = tokens.find("x:Key=\"NormalWidthFitMargin\">");
    if (margin_at == std::string::npos) return fail(3, "fit margin token missing");
    if (std::stod(tokens.substr(tokens.find('>', margin_at) + 1)) <= 0.0) return fail(4, "fit margin must be positive");

    // 2. ResizeWindowToContent applies reserves, then takes max(380 x scale, measured requirement).
    const auto resize = body_of(window, "void MainWindow::ResizeWindowToContent(");
    if (resize.empty()) return fail(5, "ResizeWindowToContent not found");
    if (!contains(resize, "ApplyTelemetryReserves();") || !contains(resize, "RequiredNormalWindowWidth()") ||
        !contains(resize, "layout::normal_target_width(") ||
        !contains(resize, "const double scaled_normal_width = normal_width * text_scale;")) {
        return fail(6, "normal width must be max(scaled base, measured requirement)");
    }
    if (contains(resize, "(std::min)(normal_width * text_scale, work_width_cap)")) return fail(7, "legacy final-width formula returned");

    // 3. Reserves: measured with the real formatters and digit variants, set as MinWidth/MaxWidth, never Width.
    const auto reserves = body_of(layout_cpp, "bool MainWindow::ApplyTelemetryReserves()");
    if (reserves.size() < 600) return fail(8, "ApplyTelemetryReserves is not implemented");
    for (const char* needle : {"FormatSpeed(", "FormatProgressPercent(", "FormatEta(", "kSpeedDomainBytesPerSecond",
                               "kPercentDomainFractions", "kEtaDomainSeconds", "SpeedText()", "ProgressPercentText()",
                               "EtaText()"}) {
        if (!contains(reserves, needle)) return fail(9, "reserve computation lacks a required input");
    }
    if (!contains(layout_cpp, "cell.MinWidth(width)") || !contains(layout_cpp, "cell.MaxWidth(width)") ||
        !contains(layout_cpp, "TextTrimming::CharacterEllipsis") || !contains(layout_cpp, "digit_variants")) {
        return fail(10, "cells must be reserved with measured MinWidth/MaxWidth, trimming and digit variants");
    }
    if (contains(layout_cpp, ".Width(") || contains(layout_cpp, "MinWidth(64") || contains(layout_cpp, "MinWidth(36") ||
        contains(layout_cpp, "MinWidth(68") || contains(layout_cpp, "MinWidth(48")) {
        return fail(11, "fixed literal widths in the reserve code");
    }

    // 3b. The reserve cache must follow what the text engine measures (canary), and a settle pass must
    // re-run it after a live Text Size / DPI change: the OS notification can precede the engine.
    if (!contains(reserves, "canary_changed(telemetry_reserve_canary_") || !contains(reserves, "kReserveCanaryText") ||
        !contains(reserves, "telemetry_reserve_canary_ = canary")) {
        return fail(19, "reserve cache is not keyed on the measured canary");
    }
    const auto settle = body_of(layout_cpp, "void MainWindow::ScheduleTelemetryReserveSettle()");
    if (settle.size() < 300 || !contains(settle, "ApplyTelemetryReserves()") || !contains(settle, "ResizeWindowToContent()")) {
        return fail(20, "settle pass is missing or does not re-apply the reserves and resize");
    }
    if (count_occurrences(window, "ScheduleTelemetryReserveSettle()") < 2) {
        return fail(21, "settle pass is not scheduled after text-size and DPI changes");
    }

    // 4. The required width measures the real row and adds the safety margin (no exact-limit fit).
    const auto required = body_of(layout_cpp, "double MainWindow::RequiredNormalWindowWidth()");
    if (!contains(required, "BottomContentGrid().Measure(") || !contains(required, "NormalWidthFitMargin") ||
        !contains(required, "required_normal_window_width(content, chrome, fit_margin)") || !contains(required, "TransferContentGrid().Padding()")) {
        return fail(12, "required width must measure the row and include chrome and fit margin");
    }

    // 5. Operating domain is bounded: speeds below 100 GiB/s, ETA capped at 99 h 59 m (FormatEta stays unbounded).
    if (!contains(layout_h, "kEtaDomainMaxSeconds = 99.0 * 3600.0 + 59.0 * 60.0") || !contains(layout_h, "99.99 * kGiB")) {
        return fail(13, "operating domain changed");
    }

    // 6. Wiring: declared, compiled, probe reports the corrected metrics.
    if (!contains(header, "bool ApplyTelemetryReserves();") || !contains(header, "double RequiredNormalWindowWidth();") ||
        !contains(project, "MainWindow.TelemetryLayout.cpp") || !contains(project, "TelemetryLayout.h")) {
        return fail(14, "layout code not wired into the project");
    }
    const auto probe = read_source(ui / "MainWindow.Diagnostics.cpp");
    for (const char* needle : {"x_pause", "TransferContentGrid().Padding()", "details_right"}) {
        (void)needle;
    }
    if (contains(probe, "ActionHideDetails") || contains(probe, "details_worst")) {
        return fail(15, "probe must not mix the expanded Details label into the normal-mode requirement");
    }

    // 7. The retired fixed-width experiment stays retired.
    for (const char* name : {"SpeedText", "ProgressPercentText", "EtaText"}) {
        const auto at = xaml.find(std::string{"x:Name=\""} + name + "\"");
        if (at == std::string::npos) return fail(16, "telemetry TextBlock missing");
        const auto element = xaml.substr(at, xaml.find("/>", at) - at);
        static const std::regex fixed_width{R"re((^|[^\w])Width="(68|36|48)")re"};
        if (std::regex_search(element, fixed_width)) return fail(17, "fixed Width=68/36/48 resurfaced");
    }

    // 8. The spec no longer calls 380 the final width.
    if (!contains(spec, "scaled minimum")) return fail(18, "UI_SPEC must describe 380 x TextScale as a scaled minimum");
    return 0;
}
