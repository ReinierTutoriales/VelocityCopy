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
        !contains(xaml, "Style=\"{StaticResource AccentIconStyle}\"") ||
        !contains(tokens, "TransferItemIconSize") ||
        !contains(tokens, "TransferItemIconMargin")) {
        return fail(2, "active transfer must expose themed document iconography");
    }

    if (!contains(tokens, "SecondaryIconStyle") ||
        !contains(ui_tokens, "apply_icon_style") ||
        !contains(xaml, "Style=\"{StaticResource SecondaryIconStyle}\"")) {
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

    // Data templates preserve document icons, native selection and themed colors.
    const auto template_begin = xaml.find("x:Key=\"QueueItemTemplate\"");
    const auto template_end = xaml.find("</Grid.Resources>", template_begin);
    const auto queue_templates = xaml.substr(template_begin, template_end - template_begin);
    if (template_begin == std::string::npos ||
        count_occurrences(queue_templates, "Glyph=\"&#xE8A5;\"") != 2 ||
        !contains(queue_templates, "QueueItemIconSize") ||
        contains(queue_templates, "ProgressBar")) {
        return fail(6, "queue templates must preserve document icons without per-item progress");
    }

    if (!contains(tokens, "SectionHeaderPadding") ||
        count_occurrences(xaml, "BorderThickness=\"0,0,0,1\"") < 2 ||
        count_occurrences(xaml, "Padding=\"{StaticResource SectionHeaderPadding}\"") < 2) {
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
    const auto show_error = body_of(window, "void MainWindow::ShowError(");
    const auto reset_surface = body_of(window, "void MainWindow::ResetTransferSurface()");
    const auto finish_copy = body_of(execution, "void MainWindow::FinishCopy(");
    if (window.empty() ||
        !contains(show_notice, "TransferProgress().ShowError(severity == InfoBarSeverity::Error)") ||
        !contains(show_notice, "ErrorBar().Severity(severity)") ||
        !contains(show_notice, "ErrorBar().Title(title)") ||
        !contains(show_error, "get_string(L\"StatusFailed\")") ||
        !contains(show_error, "ShowNotice(InfoBarSeverity::Error, title, message)") ||
        !contains(finish_copy, "get_string(L\"StatusCompletedWithIssues\")") ||
        !contains(finish_copy, "completed_with_issues_title") ||
        !contains(reset_surface, "ErrorBar().Title(L\"\")")) {
        return fail(11, "InfoBar title and severity must represent the actual terminal state independently");
    }

    if (!contains(xaml, "x:Name=\"OptionsIcon\"") ||
        !contains(xaml, "x:Name=\"DetailsChevronIcon\"") ||
        count_occurrences(xaml, "x:Key=\"ButtonForegroundPointerOver\" ResourceKey=\"TextFillColorPrimaryBrush\"") < 4 ||
        // Cancel and the queue trash are the only critical hovers.
        count_occurrences(xaml, "<StaticResource x:Key=\"ButtonForegroundPointerOver\" ResourceKey=\"SystemFillColorCriticalBrush\" />") != 2) {
        return fail(14, "secondary commands must stay subordinate while retaining native interaction states");
    }

    if (!contains(xaml, "x:Name=\"PerformanceGraphMidline\"") ||
        !contains(xaml, "Background=\"{ThemeResource CardStrokeColorDefaultBrush}\"") ||
        !contains(xaml, "Opacity=\"0.55\"")) {
        return fail(15, "performance graph must expose a subtle theme-aware midline guide");
    }

    if (!contains(tokens, "DecisionStatusIconSize") ||
        !contains(tokens, "WarningIconStyle") ||
        !contains(tokens, "SystemFillColorCautionBrush") ||
        !contains(tokens, "ErrorIconStyle") ||
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
    if (count_occurrences(queue_templates, "AutomationProperties.AccessibilityView=\"Raw\"") != 2) {
        return fail(20, "dynamic queue row icons must remain decorative in UI Automation");
    }

    if (!contains(pch, "#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>")) {
        return fail(21, "UI Automation peer enums must be included explicitly rather than transitively");
    }

    const auto transfer_visual = body_of(window, "void MainWindow::ApplyTransferVisualState(");
    if (!contains(transfer_visual, "L\"\\xE8A5\"") ||
        !contains(transfer_visual, "L\"\\xE7BA\"") ||
        !contains(transfer_visual, "L\"\\xEB90\"") ||
        !contains(transfer_visual, "L\"AccentIconStyle\"") ||
        !contains(transfer_visual, "L\"WarningIconStyle\"") ||
        !contains(transfer_visual, "L\"ErrorIconStyle\"") ||
        !contains(reset_surface, "ApplyTransferVisualState(TransferVisualState::Active)") ||
        !contains(running_state, "ApplyTransferVisualState(TransferVisualState::Active)") ||
        !contains(conflict_state, "ApplyTransferVisualState(TransferVisualState::Warning)") ||
        !contains(show_notice, "ApplyTransferVisualState(TransferVisualState::Error)") ||
        !contains(show_notice, "ApplyTransferVisualState(TransferVisualState::Warning)") ||
        !contains(execution, "SetExecutionButtonsConflict();\n        ApplyTransferVisualState(TransferVisualState::Error);")) {
        return fail(22, "document, conflict and failure visuals must map to real transfer states through one semantic helper");
    }

    // New system glyphs rely on FontIcon's documented SymbolThemeFontFamily default.
    // The eight remaining explicit Segoe Fluent declarations predate this visual branch.
    if (count_occurrences(xaml, "FontFamily=\"Segoe Fluent Icons\"") != 8 ||
        contains(queue, "item_icon.FontFamily(")) {
        return fail(23, "new system icons must use FontIcon's SymbolThemeFontFamily fallback");
    }

    const auto resize_to_content = body_of(window, "void MainWindow::ResizeWindowToContent(");
    if (!contains(xaml, "x:Name=\"QueueSectionIcon\"") ||
        !contains(resize_to_content, "QueueSectionIcon().Visibility(Visibility::Collapsed)") ||
        !contains(resize_to_content, "QueueSectionIcon().Visibility(Visibility::Visible)")) {
        return fail(24, "Queue section icon must yield header width in ThreeColumn and remain visible in Narrow");
    }

    // New fidelity work must remain theme-driven; no fixed RGB/hex palette is allowed.
    if (contains(xaml, "Color=\"#") || contains(xaml, "Background=\"#") ||
        contains(xaml, "Foreground=\"#") || contains(tokens, "Color=\"#")) {
        return fail(12, "visual fidelity must use ThemeResource instead of fixed colors");
    }

    return 0;
}
