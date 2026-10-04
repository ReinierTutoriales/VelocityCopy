#include "pch.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "TelemetryProbe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

#define VELOCITYCOPY_PROBE_STR_IMPL(x) #x
#define VELOCITYCOPY_PROBE_STR(x) VELOCITYCOPY_PROBE_STR_IMPL(x)
#ifndef VELOCITYCOPY_BUILD_SHA
#define VELOCITYCOPY_BUILD_SHA unknown
#endif

namespace {

// Telemetry geometry diagnostics. Records layout metrics only: no file names,
// no source/destination paths, no transfer data. Never mutates the UI.

TextBlock make_probe_block(TextBlock const& real) {
    TextBlock block;
    block.FontSize(real.FontSize());
    block.FontWeight(real.FontWeight());
    block.FontFamily(real.FontFamily());
    block.FontStyle(real.FontStyle());
    block.FontStretch(real.FontStretch());
    block.CharacterSpacing(real.CharacterSpacing());
    block.IsTextScaleFactorEnabled(real.IsTextScaleFactorEnabled());
    block.TextWrapping(TextWrapping::NoWrap);
    return block;
}

double text_width(TextBlock const& block, std::wstring_view text) {
    block.Text(hstring(text));
    block.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
    return block.DesiredSize().Width;
}

struct FieldMax {
    double width{};
    std::wstring text;
};

// Widest rendering over every sample and every digit variant of that sample.
FieldMax widest(TextBlock const& block, const std::vector<std::wstring>& samples,
                std::vector<std::wstring>& detail, std::wstring_view field) {
    FieldMax best;
    for (const auto& sample : samples) {
        for (const auto& variant : velocitycopy::ui::probe::digit_variants(sample)) {
            const double w = text_width(block, variant);
            if (w > best.width) {
                best.width = w;
                best.text = variant;
            }
        }
        detail.push_back(std::format(L"# sample | {} | {} | {:.1f}", field, sample, text_width(block, sample)));
    }
    return best;
}

std::wstring fixed(double value) { return std::format(L"{:.1f}", value); }

double left_of(FrameworkElement const& element, UIElement const& relative_to) {
    return element.TransformToVisual(relative_to).TransformPoint({0.0f, 0.0f}).X;
}

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

} // namespace

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::ScheduleTelemetryGeometryProbe() {
#if defined(VELOCITYCOPY_GEOMETRY_PROBE)
    try {
        if (!dispatcher_) return;
        if (!geometry_probe_timer_) {
            geometry_probe_timer_ = dispatcher_.CreateTimer();
            geometry_probe_timer_.IsRepeating(false);
            geometry_probe_timer_.Interval(std::chrono::milliseconds(600));
            geometry_probe_timer_.Tick([weak = get_weak()](
                Microsoft::UI::Dispatching::DispatcherQueueTimer const&, Windows::Foundation::IInspectable const&) {
                if (auto self = weak.get()) self->LogTelemetryGeometry();
            });
        }
        // Debounce: one record after the layout has been quiet for the interval.
        geometry_probe_timer_.Stop();
        geometry_probe_timer_.Start();
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: telemetry probe scheduling failed\n");
    }
#endif
}

void MainWindow::LogTelemetryGeometry() {
    namespace probe = velocitycopy::ui::probe;
    try {
        constexpr double mib = 1024.0 * 1024.0;
        constexpr double gib = 1024.0 * mib;

        // Hard worst case (diagnostic): wider than the operating domain the layout reserves.
        // Samples go through the real formatters, never through literal strings.
        std::vector<std::wstring> speed_samples;
        for (const double bytes_per_second : {0.0, 1023.0 * 1024.0, 1048575.0, 1073741823.0, 999.9 * mib,
                                              10.0 * gib, 1023.99 * gib}) {
            speed_samples.emplace_back(FormatSpeed(bytes_per_second).c_str());
        }
        std::vector<std::wstring> pct_samples;
        for (const double fraction : {0.0, 0.0005, 0.013, 0.099, 0.5, 0.999, 1.0}) {
            pct_samples.emplace_back(FormatProgressPercent(fraction).c_str());
        }
        // 99 h 59 m is the agreed operating cap; FormatEta() itself stays unbounded.
        std::vector<std::wstring> eta_samples;
        for (const double seconds : {0.0, 59.0, 3599.0, 3600.0, 99.0 * 3600.0 + 59.0 * 60.0}) {
            eta_samples.emplace_back(FormatEta(seconds).c_str());
        }
        const bool eta_unbounded = probe::eta_hours_exceed_two_digits(FormatEta(100.0 * 3600.0).c_str());

        std::vector<std::wstring> detail;
        const auto speed_block = make_probe_block(SpeedText());
        const auto pct_block = make_probe_block(ProgressPercentText());
        const auto eta_block = make_probe_block(EtaText());
        const auto speed_max = widest(speed_block, speed_samples, detail, L"speed");
        const auto pct_max = widest(pct_block, pct_samples, detail, L"pct");
        const auto eta_max = widest(eta_block, eta_samples, detail, L"eta");

        // Self-validation. (a) the synthetic block reproduces the real cell's DesiredSize
        // (floored by the runtime reserve); (b) informational: text-only delta vs ActualWidth.
        std::vector<std::wstring> failed;
        const auto validate = [&](TextBlock const& real, TextBlock const& synthetic, std::wstring_view name) {
            const double text = text_width(synthetic, real.Text().c_str());
            const double expected = (std::max)(text, real.MinWidth());
            const double actual = real.DesiredSize().Width;
            detail.push_back(std::format(
                L"# validate | {} | synthetic={:.1f} | real={:.1f} | text_delta={:.1f}", name, expected, actual,
                text - real.ActualWidth()));
            if (actual <= 0.0 || !probe::widths_match(expected, actual)) failed.emplace_back(name);
        };
        validate(SpeedText(), speed_block, L"speed");
        validate(ProgressPercentText(), pct_block, L"pct");
        validate(EtaText(), eta_block, L"eta");

        // Runtime reserves actually applied to the cells (MinWidth == MaxWidth).
        const double speed_cell = SpeedText().MinWidth();
        const double pct_cell = ProgressPercentText().MinWidth();
        const double eta_cell = EtaText().MinWidth();
        const double strip_spacing = TelemetryStrip().Spacing();
        const double tele = speed_cell + pct_cell + eta_cell + strip_spacing * 2.0;

        const double cluster = PrimaryActionCluster().DesiredSize().Width;
        const double cluster_spacing = PrimaryActionCluster().Spacing();
        const double column_spacing = BottomContentGrid().ColumnSpacing();
        // Natural width of Details in the CURRENT mode (its label only changes with the mode).
        const double details = DetailsButton().DesiredSize().Width;

        // Real available width (window content minus chrome), not the grid's own ActualWidth,
        // which is inflated when the row overflows.
        const auto content_padding = TransferContentGrid().Padding();
        const auto bottom_margin = BottomContentGrid().Margin();
        const double avail = TransferContentGrid().ActualWidth() - content_padding.Left - content_padding.Right -
                             bottom_margin.Left - bottom_margin.Right;
        const double required = tele + cluster + details + column_spacing * 3.0;
        const double holgura = avail - required;
        const double declared_total = strip_spacing * 2.0 + cluster_spacing * 2.0 + column_spacing * 3.0;

        std::wstring live_gaps;
        std::vector<std::wstring> gate(5, L"?");
        try {
            const UIElement grid = BottomContentGrid();
            const UIElement root = RootGrid();
            const double speed_x = left_of(SpeedText(), grid);
            const double pct_x = left_of(ProgressPercentText(), grid);
            const double eta_x = left_of(EtaText(), grid);
            const double strip_x = left_of(TelemetryStrip(), grid);
            const double cluster_x = left_of(PrimaryActionCluster(), grid);
            const double details_x = left_of(DetailsButton(), grid);
            live_gaps = std::format(L"s>p={} p>e={} t>c={} c>d={}",
                fixed(pct_x - (speed_x + SpeedText().DesiredSize().Width)),
                fixed(eta_x - (pct_x + ProgressPercentText().DesiredSize().Width)),
                fixed(cluster_x - (strip_x + TelemetryStrip().DesiredSize().Width)),
                fixed(details_x - (cluster_x + cluster)));
            // Gate invariants: X of the actions and distance from Details to the window edge.
            const double details_root_x = left_of(DetailsButton(), root);
            gate = {fixed(left_of(PauseButtonHost(), root)), fixed(left_of(CancelButtonHost(), root)),
                    fixed(left_of(OptionsButton(), root)), fixed(details_root_x),
                    fixed(RootGrid().ActualWidth() - (details_root_x + details))};
        } catch (...) {
            live_gaps = L"unavailable";
            gate.assign(5, L"?");
            failed.emplace_back(L"geometry");
        }

        RECT window_rect{};
        const bool has_rect = hwnd_ != nullptr && GetWindowRect(hwnd_, &window_rect);
        const std::wstring hwnd_px = has_rect
            ? std::format(L"{}x{}", window_rect.right - window_rect.left, window_rect.bottom - window_rect.top)
            : std::wstring{L"?"};
        const std::wstring mode = !expanded_ ? L"normal"
            : (expanded_layout_mode_ == ExpandedLayoutMode::Narrow ? L"exp-narrow" : L"exp-3col");
        const std::wstring build =
            std::wstring{L"" VELOCITYCOPY_PROBE_STR(VELOCITYCOPY_BUILD_SHA)}
#if defined(VELOCITYCOPY_GEOMETRY_PROBE)
            + L"+probe";
#else
            + L"-noprobe";
#endif
        const std::wstring validation = failed.empty() ? std::wstring{L"OK"} : [&] {
            std::wstring text = L"FAIL:";
            for (std::size_t i = 0; i < failed.size(); ++i) text += (i ? L"," : L"") + failed[i];
            return text;
        }();

        const std::vector<std::wstring> fields{
            build, mode, hwnd_px, std::format(L"{:.2f}", last_text_scale_factor_),
            std::format(L"{:.2f}", last_rasterization_scale_), fixed(speed_max.width), fixed(pct_max.width),
            fixed(eta_max.width),
            std::format(L"speed={} pct={} eta={}", fixed(speed_cell), fixed(pct_cell), fixed(eta_cell)),
            std::format(L"strip={}x2 cluster={}x2 cols={}x3 total={}", fixed(strip_spacing), fixed(cluster_spacing),
                        fixed(column_spacing), fixed(declared_total)),
            live_gaps, fixed(tele), fixed(cluster), fixed(details), fixed(avail), fixed(required), fixed(holgura)};
        auto all_fields = fields;
        all_fields.insert(all_fields.end(), gate.begin(), gate.end());
        all_fields.push_back(paused_ ? L"1" : L"0");
        all_fields.push_back(eta_unbounded ? L"true" : L"false");
        all_fields.push_back(validation);
        const std::wstring line = probe::join_fields(all_fields);

        if (line == last_geometry_line_) return;
        last_geometry_line_ = line;

        std::wstring record;
        if (!geometry_probe_run_marked_) {
            geometry_probe_run_marked_ = true;
            record += std::wstring{L"# run "} + build + L"\n# columns: " + std::wstring{probe::kLogColumns} + L"\n";
        }
        for (const auto& item : detail) record += item + L"\n";
        record += line + L"\n";

        OutputDebugStringW((L"VelocityCopy geometry: " + line + L"\n").c_str());
        if (const auto dir = velocitycopy::app_data_directory()) {
            const auto folder = *dir / L"Diagnostics";
            std::error_code ec;
            std::filesystem::create_directories(folder, ec);
            std::ofstream out(folder / L"telemetry-geometry.log", std::ios::binary | std::ios::app);
            if (out) {
                const auto utf8 = to_utf8(record);
                out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
            }
        }
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: LogTelemetryGeometry failed\n");
    }
}

} // namespace winrt::VelocityCopyUI::implementation
