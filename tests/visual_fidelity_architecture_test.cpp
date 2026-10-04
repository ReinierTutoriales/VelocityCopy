#include "architecture_support.hpp"

#include <iostream>
#include <regex>
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
    const auto ui_tokens = read_source(root / "src/ui/VelocityCopy.UI/UiTokens.h");
    const auto pch = read_source(root / "src/ui/VelocityCopy.UI/pch.h");
    if (xaml.empty() || queue.empty() || tokens.empty() || ui_tokens.empty() || pch.empty()) return fail(1, "required UI source missing");

    // Active transfer identity: a Fluent document mark sits beside the essential data.
    if (!contains(xaml, "x:Name=\"CurrentItemIcon\"") ||
        !contains(xaml, "Glyph=\"&#xE8A5;\"") ||
        !contains(xaml, "Foreground=\"{ThemeResource AccentFillColorDefaultBrush}\"") ||
        !contains(tokens, "TransferItemIconSize") ||
        !contains(tokens, "TransferItemIconMargin")) {
        return fail(2, "active transfer must expose themed document iconography");
    }

    if (!contains(tokens, "SecondaryIconStyle") ||
        !contains(ui_tokens, "apply_icon_style") ||
        !contains(queue, "apply_icon_style(item_icon, L\"SecondaryIconStyle\")")) {
        return fail(13, "dynamic queue icons must inherit the shared theme-aware secondary icon style");
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

    if (!contains(tokens, "SectionHeaderPadding") ||
        count_occurrences(xaml, "BorderThickness=\"0,0,0,1\"") < 3 ||
        count_occurrences(xaml, "Padding=\"{StaticResource SectionHeaderPadding}\"") < 3) {
        return fail(7, "expanded cards must separate headers from content with the shared themed divider");
    }

    if (!contains(tokens, "QueueCountBadgePadding") ||
        !contains(tokens, "QueueCountBadgeMargin") ||
        !contains(xaml, "x:Name=\"QueueCountText\"") ||
        !contains(xaml, "Padding=\"{StaticResource QueueCountBadgePadding}\"") ||
        !contains(xaml, "BorderBrush=\"{ThemeResource CardStrokeColorDefaultBrush}\"")) {
        return fail(8, "queue count must read as a neutral themed badge");
    }

    // Button icon colors use WinUI lightweight-styling resources so disabled, hover and pressed
    // states remain owned by the native Button template instead of being bypassed on FontIcon children.
    if (!contains(xaml, "x:Key=\"ButtonForeground\" ResourceKey=\"AccentFillColorDefaultBrush\"") ||
        !contains(xaml, "x:Key=\"ButtonForeground\" ResourceKey=\"SystemFillColorCriticalBrush\"") ||
        !contains(xaml, "x:Key=\"ButtonForegroundDisabled\" ResourceKey=\"TextFillColorDisabledBrush\"") ||
        count_occurrences(xaml, "x:Key=\"ButtonForeground\" ResourceKey=\"TextFillColorSecondaryBrush\"") < 5 ||
        contains(xaml, "x:Name=\"PauseIcon\"\n                                              FontFamily=\"Segoe Fluent Icons\"\n                                              Glyph=\"&#xE769;\"\n                                              FontSize=\"{StaticResource ActionIconSize}\"\n                                              Foreground=") ||
        contains(xaml, "x:Name=\"CancelIcon\"\n                                              FontFamily=\"Segoe Fluent Icons\"\n                                              Glyph=\"&#xE71A;\"\n                                              FontSize=\"{StaticResource ActionIconSize}\"\n                                              Foreground=") ||
        !contains(xaml, "x:Name=\"PerformanceCurrentSpeedText\"") ||
        !contains(xaml, "FontWeight=\"SemiBold\"")) {
        return fail(10, "action hierarchy must use native Button lightweight styling and preserve disabled states");
    }

    const auto execution = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto running_state = body_of(execution, "void MainWindow::SetExecutionButtonsRunning()");
    const auto paused_state = body_of(execution, "void MainWindow::SetExecutionButtonsStopped()");
    const auto conflict_state = body_of(execution, "void MainWindow::SetExecutionButtonsConflict()");
    if (!contains(running_state, "TransferProgress().ShowPaused(false)") ||
        !contains(running_state, "TransferProgress().ShowError(false)") ||
        !contains(paused_state, "TransferProgress().ShowPaused(true)") ||
        !contains(paused_state, "TransferProgress().ShowError(false)") ||
        !contains(conflict_state, "TransferProgress().ShowPaused(false)") ||
        !contains(conflict_state, "TransferProgress().ShowError(false)")) {
        return fail(16, "running, paused and conflict states must map only to truthful native ProgressBar visual states");
    }
    if (execution.empty() ||
        !contains(xaml, "x:Name=\"DetailsSourceText\" Grid.Column=\"2\" TextWrapping=\"NoWrap\" TextTrimming=\"CharacterEllipsis\" MaxLines=\"1\"") ||
        !contains(xaml, "x:Name=\"DetailsDestinationText\" Grid.Row=\"1\" Grid.Column=\"2\" TextWrapping=\"NoWrap\" TextTrimming=\"CharacterEllipsis\" MaxLines=\"1\"") ||
        !contains(execution, "ToolTipService::SetToolTip(DetailsSourceText(), box_value(source))") ||
        !contains(execution, "ToolTipService::SetToolTip(DetailsDestinationText(), box_value(destination))")) {
        return fail(9, "information paths must stay single-line with ellipsis and preserve full paths in tooltips");
    }

    const auto window = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto show_notice = body_of(window, "void MainWindow::ShowNotice(");
    const auto reset_surface = body_of(window, "void MainWindow::ResetTransferSurface()");
    if (window.empty() ||
        !contains(show_notice, "TransferProgress().ShowError(severity == InfoBarSeverity::Error)") ||
        !contains(show_notice, "ErrorBar().Severity(severity)") ||
        !contains(show_notice, "ErrorBar().Title(velocitycopy::localization::get_string(L\"StatusFailed\"))") ||
        !contains(show_notice, "ErrorBar().Title(velocitycopy::localization::get_string(L\"StatusCompletedWithIssues\"))") ||
        !contains(reset_surface, "ErrorBar().Title(L\"\")")) {
        return fail(11, "native InfoBar must expose localized semantic titles and reset them between sessions");
    }

    if (!contains(xaml, "x:Name=\"OptionsIcon\"") ||
        !contains(xaml, "x:Name=\"DetailsChevronIcon\"") ||
        count_occurrences(xaml, "x:Key=\"ButtonForegroundPointerOver\" ResourceKey=\"TextFillColorPrimaryBrush\"") < 5) {
        return fail(14, "secondary commands must stay subordinate while retaining native interaction states");
    }

    if (!contains(xaml, "x:Name=\"PerformanceGraphMidline\"") ||
        !contains(xaml, "Background=\"{ThemeResource CardStrokeColorDefaultBrush}\"") ||
        !contains(xaml, "Opacity=\"0.55\"")) {
        return fail(15, "performance graph must expose a subtle theme-aware midline guide");
    }

    if (!contains(tokens, "DecisionStatusIconSize") ||
        !contains(tokens, "DecisionWarningIconStyle") ||
        !contains(tokens, "SystemFillColorCautionBrush") ||
        !contains(tokens, "DecisionErrorIconStyle") ||
        !contains(tokens, "SystemFillColorCriticalBrush")) {
        return fail(17, "decision states must use documented Fluent warning/error icon semantics");
    }

    if (!std::regex_search(xaml, std::regex{R"(<FontIcon[^>]*x:Name="CurrentItemIcon"[^>]*AutomationProperties\.AccessibilityView="Raw"[^>]*/>)"})) {
        return fail(18, "current-item status icon must remain decorative in UI Automation");
    }
    for (int row = 0; row < 6; ++row) {
        const std::regex information_icon{
            "<FontIcon[^>]*Grid.Row=\\\"" + std::to_string(row) +
            "\\\"[^>]*InformationIconSize[^>]*AutomationProperties\\.AccessibilityView=\\\"Raw\\\"[^>]*/>"};
        if (!std::regex_search(xaml, information_icon)) {
            return fail(19, "information-row icons must remain decorative in UI Automation");
        }
    }
    if (!contains(queue, "AutomationProperties::SetAccessibilityView(") ||
        !contains(queue, "AccessibilityView::Raw")) {
        return fail(20, "dynamic queue row icons must remain decorative in UI Automation");
    }

    if (!contains(pch, "#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>")) {
        return fail(21, "UI Automation peer enums must be included explicitly rather than transitively");
    }

    // New fidelity work must remain theme-driven; no fixed RGB/hex palette is allowed.
    if (contains(xaml, "Color=\"#") || contains(xaml, "Background=\"#") ||
        contains(xaml, "Foreground=\"#") || contains(tokens, "Color=\"#")) {
        return fail(12, "visual fidelity must use ThemeResource instead of fixed colors");
    }

    return 0;
}
