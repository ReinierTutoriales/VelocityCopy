#include "pch.h"
#include "MainWindow.xaml.h"
#include "TelemetryLayout.h"
#include "UiTokens.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

// Detached TextBlock carrying the typography of a real cell, so the XAML text engine
// (font, weight, Text Size scale) measures the sample exactly as the cell will render it.
TextBlock make_measure_block(TextBlock const& real) {
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

// Widest rendering over every sample and every digit variant of that sample.
double widest_width(TextBlock const& block, const std::vector<std::wstring>& samples) {
    double widest = 0.0;
    for (const auto& sample : samples) {
        for (const auto& variant : velocitycopy::ui::layout::digit_variants(sample)) {
            block.Text(hstring(variant));
            block.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
            widest = (std::max)(widest, static_cast<double>(block.DesiredSize().Width));
        }
    }
    return std::ceil(widest);
}

// The reserve is a measured cell width, not a literal: values outside the operating domain
// are trimmed instead of pushing the action cluster.
void apply_reserve(TextBlock const& cell, const double width) {
    if (width <= 0.0) return;
    cell.MinWidth(width);
    cell.MaxWidth(width);
    cell.TextTrimming(TextTrimming::CharacterEllipsis);
}

} // namespace

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::ApplyTelemetryReserves() {
    namespace layout = velocitycopy::ui::layout;
    try {
        // Reserves only depend on the text engine inputs; recompute when those change.
        if (telemetry_reserves_applied_ &&
            std::abs(last_text_scale_factor_ - telemetry_reserve_text_scale_) <= 0.0001 &&
            std::abs(last_rasterization_scale_ - telemetry_reserve_raster_) <= 0.0001) {
            return;
        }

        std::vector<std::wstring> speeds;
        for (const double bytes_per_second : layout::kSpeedDomainBytesPerSecond) {
            speeds.emplace_back(FormatSpeed(bytes_per_second).c_str());
        }
        std::vector<std::wstring> percents;
        for (const double fraction : layout::kPercentDomainFractions) {
            percents.emplace_back(FormatProgressPercent(fraction).c_str());
        }
        std::vector<std::wstring> etas;
        for (const double seconds : layout::kEtaDomainSeconds) {
            etas.emplace_back(FormatEta(seconds).c_str());
        }

        apply_reserve(SpeedText(), widest_width(make_measure_block(SpeedText()), speeds));
        apply_reserve(ProgressPercentText(), widest_width(make_measure_block(ProgressPercentText()), percents));
        apply_reserve(EtaText(), widest_width(make_measure_block(EtaText()), etas));

        telemetry_reserve_text_scale_ = last_text_scale_factor_;
        telemetry_reserve_raster_ = last_rasterization_scale_;
        telemetry_reserves_applied_ = true;
        BottomContentGrid().InvalidateMeasure();
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: ApplyTelemetryReserves failed\n");
    }
}

double MainWindow::RequiredNormalWindowWidth() {
    namespace layout = velocitycopy::ui::layout;
    try {
        ApplyTelemetryReserves();
        // Row content at unconstrained width: the * spacer collapses to zero, so this is
        // reserved telemetry + actions + Details at its natural width + the three column gaps.
        constexpr float unbounded = std::numeric_limits<float>::infinity();
        BottomContentGrid().Measure({unbounded, unbounded});
        const double content = BottomContentGrid().DesiredSize().Width;
        BottomContentGrid().InvalidateMeasure();

        const auto padding = TransferContentGrid().Padding();
        const auto margin = BottomContentGrid().Margin();
        const double chrome = padding.Left + padding.Right + margin.Left + margin.Right;
        const double fit_margin = velocitycopy::ui::token_double(L"NormalWidthFitMargin", 8);
        return layout::required_normal_window_width(content, chrome, fit_margin);
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: RequiredNormalWindowWidth failed\n");
        return 0.0;
    }
}

} // namespace winrt::VelocityCopyUI::implementation
