#include "architecture_support.hpp"

#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(int code, const char* message) {
    std::cerr << "visual fidelity architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto xaml = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto queue = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto tokens = read_source(root / "src/ui/DesignTokens.xaml");
    if (xaml.empty() || queue.empty() || tokens.empty()) return fail(1, "required UI source missing");

    // Active transfer identity: a Fluent document mark sits beside the essential data.
    if (!contains(xaml, "x:Name=\"CurrentItemIcon\"") ||
        !contains(xaml, "Glyph=\"&#xE8A5;\"") ||
        !contains(xaml, "Foreground=\"{ThemeResource AccentFillColorDefaultBrush}\"") ||
        !contains(tokens, "TransferItemIconSize") ||
        !contains(tokens, "TransferItemIconMargin")) {
        return fail(2, "active transfer must expose themed document iconography");
    }

    // Expanded cards share one Fluent visual hierarchy.
    if (!contains(xaml, "Glyph=\"&#xE9D2;\"") ||
        !contains(xaml, "Glyph=\"&#xE946;\"") ||
        !contains(xaml, "x:Name=\"QueueTitle\"") ||
        !contains(tokens, "SectionIconSize") ||
        !contains(tokens, "SectionIconMargin")) {
        return fail(3, "queue, performance and information headers must carry themed Fluent icons");
    }

    // Information rows use semantic icons while preserving the existing localized labels.
    for (const char* label : {
            "DetailsSourceLabel", "DetailsDestinationLabel", "DetailsBytesLabel",
            "DetailsFilesLabel", "DetailsSpeedLabel", "DetailsEtaLabel"}) {
        if (!contains(xaml, label)) return fail(4, "localized information label missing");
    }
    if (!contains(xaml, "InformationIconSize") ||
        !contains(xaml, "TextFillColorSecondaryBrush")) {
        return fail(5, "information iconography must use theme-aware secondary color");
    }

    // Queue rows are generated dynamically; every visual gets the document glyph without
    // replacing selection/focus behavior or introducing per-item progress.
    if (!contains(queue, "FontIcon item_icon") ||
        !contains(queue, "item_icon.Glyph(L\"\\xE8A5\")") ||
        !contains(queue, "QueueItemIconSize") ||
        !contains(queue, "name_line.Children().Append(item_icon)") ||
        contains(queue, "ProgressBar")) {
        return fail(6, "queue rows must use document icons without fake per-item progress");
    }

    // Action and live-performance emphasis use system semantic brushes rather than bespoke colors.
    if (!contains(xaml, "x:Name=\"PauseIcon\"") ||
        !contains(xaml, "SystemFillColorCriticalBrush") ||
        !contains(xaml, "x:Name=\"DetailsChevronIcon\"") ||
        !contains(xaml, "x:Name=\"PerformanceCurrentSpeedText\"") ||
        !contains(xaml, "FontWeight=\"SemiBold\"")) {
        return fail(7, "action and performance emphasis must use system semantic color hierarchy");
    }

    // New fidelity work must remain theme-driven; no fixed RGB/hex palette is allowed.
    if (contains(xaml, "Color=\"#") || contains(xaml, "Background=\"#") ||
        contains(xaml, "Foreground=\"#") || contains(tokens, "Color=\"#")) {
        return fail(8, "visual fidelity must use ThemeResource instead of fixed colors");
    }

    return 0;
}
