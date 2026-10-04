#include "architecture_support.hpp"

#include <regex>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {

bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}

std::string brace_body_at(const std::string& text, const std::size_t open) {
    if (open == std::string::npos || open >= text.size() || text[open] != '{') return {};
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        else if (text[i] == '}' && --depth == 0) return text.substr(open + 1, i - open - 1);
    }
    return {};
}

int fail(const int code, const char* message) {
    std::cerr << "compact UI architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto xaml = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto execution = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto menu = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto queue_persistence = menu;
    const auto window = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tokens = read_source(root / "src/ui/DesignTokens.xaml");
    const auto spec = read_source(root / "docs/UI_SPEC.md");
    const auto conflict = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Conflict.cpp");
    const auto about = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.About.cpp");
    bool stray_expanded_visibility_writer = false;
    bool stray_queue_button_reference = false;
    const auto ui_dir = root / "src/ui/VelocityCopy.UI";
    for (const auto& entry : std::filesystem::directory_iterator(ui_dir)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (!name.starts_with("MainWindow") || entry.path().extension() != ".cpp" ||
            name == "MainWindow.xaml.cpp") continue;
        const auto source = read_source(entry.path());
        if (contains(source, "QueueButton")) stray_queue_button_reference = true;
        if (contains(source, "QueuePanel().Visibility(") ||
            contains(source, "DetailsPanel().Visibility(")) {
            stray_expanded_visibility_writer = true;
        }
    }

    if (xaml.empty() || header.empty() || execution.empty() || queue.empty() ||
        menu.empty() || window.empty() || tokens.empty() || spec.empty() || conflict.empty() || about.empty()) {
        return fail(1, "required UI source missing");
    }

    if (!contains(queue, "expanded_layout_mode_ == ExpandedLayoutMode::Narrow") ||
        !contains(queue, "QueueNarrowItemMargin") ||
        !contains(queue, "QueueNarrowLocationMaxWidth") ||
        !contains(queue, "ToolTipService::SetToolTip(row") ||
        !contains(queue, "AutomationProperties::SetName(row") ||
        !contains(queue, "RefreshQueue(const bool force_visual_rebuild)") ||
        !contains(queue, "unchanged && !rebuild_visuals") ||
        !contains(window, "RefreshQueue(true)") ||
        !contains(window, "previous_layout_mode != expanded_layout_mode_")) {
        return fail(47, "queue rows must compact in Narrow without replacing native ListViewItem behavior and must rebuild when layout mode changes");
    }

    if (!contains(xaml, "x:Name=\"TransferSurface\"") ||
        !contains(xaml, "x:Name=\"TransferProgress\"") ||
        !contains(xaml, "x:Name=\"BrandLogo\"") ||
        !contains(xaml, "x:Name=\"TransferDetailGrid\"") ||
        !contains(xaml, "x:Name=\"TransferBytesText\"") ||
        !contains(xaml, "x:Name=\"TransferFilesText\"") ||
        !contains(xaml, "x:Name=\"TransferPathGrid\"") ||
        !contains(xaml, "x:Name=\"SourcePathText\"") ||
        !contains(xaml, "x:Name=\"DestinationPathText\"") ||
        !contains(xaml, "x:Name=\"BottomContentGrid\"") ||
        !contains(xaml, "x:Name=\"TelemetryStrip\"") ||
        !contains(xaml, "x:Name=\"PrimaryActionCluster\"") ||
        !contains(xaml, "HorizontalAlignment=\"Right\"") ||
        !contains(xaml, "x:Name=\"PauseButton\"") ||
        !contains(xaml, "x:Name=\"CancelButton\"") ||
        !contains(xaml, "x:Name=\"OptionsButton\"") ||
        !contains(xaml, "x:Name=\"DetailsButton\"") ||
        contains(xaml, "x:Name=\"QueueButton\"")) {
        return fail(2, "collapsed surface must separate telemetry from the right-aligned primary actions");
    }

    const auto caption_start = xaml.find("x:Name=\"CaptionContentGrid\"");
    const auto bottom_start = xaml.find("x:Name=\"BottomContentGrid\"");
    const auto speed_start = xaml.find("x:Name=\"SpeedText\"");
    if (caption_start == std::string::npos || bottom_start == std::string::npos || speed_start == std::string::npos ||
        speed_start < bottom_start || (bottom_start > caption_start && contains(xaml.substr(caption_start, bottom_start - caption_start), "SpeedText"))) {
        return fail(3, "telemetry must not share the caption-constrained filename row");
    }

    // UI_SPEC: the compact surface has four actions total. Pause/Resume, Cancel
    // and Options stay in the operational cluster; Details is isolated at the far
    // right. Skip and Stop remain Options menu commands only.
    const auto refresh_menu = body_of(menu, "void MainWindow::RefreshExecutionMenuState(");
    const auto cluster_start = xaml.find("x:Name=\"PrimaryActionCluster\"");
    const auto cluster_end = cluster_start == std::string::npos ? std::string::npos
                                                                : xaml.find("</StackPanel>", cluster_start);
    const auto clustered_action_count = cluster_end == std::string::npos
        ? 0
        : count_occurrences(xaml.substr(cluster_start, cluster_end - cluster_start), "<Button");
    if (contains(xaml, "SkipButton") || contains(xaml, "StopButton") ||
        contains(xaml, "OnSkipClick") || contains(xaml, "OnStopClick") ||
        contains(execution, "SkipButton()") || contains(execution, "StopButton()") ||
        contains(window, "SkipButton()") || contains(window, "StopButton()") ||
        contains(header, "RefreshExecutionButtonState") ||
        contains(tokens, "SkipIconSize") || contains(tokens, "StopIconSize") ||
        clustered_action_count != 3 ||
        count_occurrences(xaml, "x:Name=\"DetailsButton\"") != 1 ||
        !contains(menu, "skip_menu_item_.Click({this, &MainWindow::OnMenuSkipClick})") ||
        !contains(menu, "stop_menu_item_.Click({this, &MainWindow::OnMenuStopClick})") ||
        !contains(refresh_menu, "skip_menu_item_.IsEnabled(velocitycopy::can_skip_current_file(") ||
        !contains(refresh_menu, "stop_menu_item_.IsEnabled(") ||
        !contains(menu, "menu.Opening")) {
        return fail(4, "skip and stop must exist only as Options menu commands with dynamic enablement");
    }

    // Iconography: one glyph, one meaning. Disclosure owns the chevrons; queue
    // reordering uses arrows; removing a queue entry must not read as deleting a
    // file; Cancel must not reuse the window-close X.
    const auto glyph_of = [&](const std::string& button) {
        const auto start = xaml.find("x:Name=\"" + button + "\"");
        if (start == std::string::npos) return std::string{};
        const auto end = xaml.find("</Button>", start);
        const auto element = xaml.substr(start, end - start);
        const auto glyph = element.find("Glyph=\"");
        return glyph == std::string::npos ? std::string{} : element.substr(glyph + 7, 8);
    };
    if (
        glyph_of("QueueMoveUpButton") != "&#xE74A;" ||
        glyph_of("QueueMoveDownButton") != "&#xE74B;" ||
        glyph_of("QueueRemoveButton") != "&#xE738;" ||
        glyph_of("CancelButton") != "&#xE71A;" ||
        contains(xaml, "&#xE74D;") || contains(xaml, "&#xE711;") || contains(xaml, "&#xE8BB;")) {
        return fail(16, "action glyphs must be unambiguous and match their command semantics");
    }

    if (!contains(xaml, "<ProgressBar x:Name=\"TransferProgress\"") ||
        !contains(tokens, "<x:Double x:Key=\"TransferProgressHeight\">8</x:Double>") ||
        !contains(xaml, "x:Name=\"DetailsButtonText\"") ||
        !contains(xaml, "x:Name=\"DetailsChevronIcon\"") ||
        !contains(window, "DetailsButtonText().Text(velocitycopy::localization::get_string(L\"ActionDetails\"))") ||
        !contains(window, "get_string(L\"ActionDetails\")") ||
        !contains(window, "DetailsButtonText().Text(details_button_label)") ||
        !contains(window, "DetailsChevronIcon().Glyph(expanded ? L\"\\uE70E\" : L\"\\uE70D\")") ||
        contains(xaml, "x:Name=\"ProgressFill\"") ||
        !contains(window, "TransferProgress().Value(percent)") ||
        contains(window, "ProgressFill().Width(")) {
        return fail(5, "compact surface must use one native progress indicator driven by the centralized progress fraction");
    }

    // DesignTokens.xaml is the single width source; C++ reads it through the
    // UiTokens.h accessor with an identical fallback.
    if (!contains(tokens, "<x:Double x:Key=\"NormalWindowMinWidth\">380</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"ExpandedPreferredWidth\">880</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"ExpandedThreeColumnThreshold\">720</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"ExpandedWorkAreaMargin\">16</x:Double>") ||
        !contains(window, "token_double(L\"NormalWindowMinWidth\", 380)") ||
        !contains(window, "const double work_width_cap = (std::max)(normal_width, work_width_epx - work_margin * 2.0);") ||
        // 380 epx x TextScale is the scaled minimum, not the final width: the window grows only to
        // fit the reserved bottom row (see telemetry_layout_architecture_test).
        !contains(window, "const double scaled_normal_width = normal_width * text_scale;") ||
        !contains(window, "velocitycopy::ui::layout::normal_target_width(") ||
        !contains(window, "scaled_normal_width, required_normal_width, work_width_cap);") ||
        !contains(window, ": normal_target_width;") ||
        !contains(window, "const double effective_width = target_width / text_scale;") ||
        !contains(window, "expanded_layout_mode_ = effective_width >= three_column_threshold") ||
        !contains(window, "TransferSurface().InvalidateMeasure();") ||
        contains(window, "token_int(L\"CompactWindowWidth\"") ||
        contains(tokens, "CompactWindowWidth") ||
        contains(tokens, "CompactSurfaceHeight") ||
        contains(xaml, "Height=\"{StaticResource CompactSurfaceHeight}\"") ||
        contains(window, "token_int(L\"CompactSurfaceHeight\"")) {
        return fail(6, "normal geometry must use one width token and measured content height");
    }

    if (!contains(window, "presenter.IsMinimizable(true)") ||
        !contains(window, "presenter.IsMaximizable(false)") ||
        !contains(window, "presenter.IsResizable(false)") ||
        contains(window, "SetBorderAndTitleBar(true, false)")) {
        return fail(7, "native Windows caption buttons must remain visible while resize/maximize stay constrained");
    }

    if (!contains(xaml, "x:Name=\"CaptionContentGrid\"") ||
        !contains(header, "void ApplyTitleBarInset() noexcept") ||
        !contains(header, "base_caption_content_padding_") ||
        !contains(window, "const auto title_bar = AppWindow().TitleBar();") ||
        !contains(window, "title_bar.RightInset()") ||
        !contains(window, "CaptionContentGrid().Padding") ||
        contains(window, "TransferContentGrid().Padding(Thickness{")) {
        return fail(8, "caption inset must affect only the top filename row, not telemetry/actions");
    }

    if (!contains(xaml, "x:Name=\"TitleBarDragRegion\"") ||
        !contains(window, "SetTitleBar(TitleBarDragRegion())") ||
        !contains(xaml, "AllowDrop=\"True\"") ||
        !contains(window, "RootGrid().AddHandler(") ||
        !contains(window, "UIElement::DropEvent()") ||
        !contains(window, "box_value(DragEventHandler{this, &MainWindow::OnDrop})") ||
        !contains(window, "true);")) {
        return fail(9, "custom drag region and whole-window append drop must remain wired");
    }

    if (!contains(menu, "skip_menu_item_.Text") || !contains(menu, "stop_menu_item_.Text") ||
        !contains(menu, "skip_menu_item_.Click({this, &MainWindow::OnMenuSkipClick})") ||
        !contains(menu, "stop_menu_item_.Click({this, &MainWindow::OnMenuStopClick})") ||
        !contains(menu, "menu.Opening") || !contains(menu, "RefreshExecutionMenuState()")) {
        return fail(10, "secondary transfer commands must refresh state whenever Options opens");
    }

    if (contains(header, "OnMenuPauseClick") || contains(header, "OnMenuCancelClick") ||
        contains(header, "pause_menu_item_") || contains(header, "cancel_menu_item_") ||
        contains(header, "initial_size_applied_") ||
        contains(menu, "void MainWindow::OnMenuPauseClick") || contains(menu, "void MainWindow::OnMenuCancelClick")) {
        return fail(11, "unused menu wrappers and compact-size state must not accumulate as dead code");
    }

    if (!contains(tokens, "<x:Double x:Key=\"CaptionRowHeight\">32</x:Double>") ||
        !contains(tokens, "<GridLength x:Key=\"CaptionRowGridLength\">32</GridLength>") ||
        !contains(tokens, "<x:Double x:Key=\"TelemetrySpeedMinWidth\">64</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"TelemetryPercentMinWidth\">36</x:Double>") ||
        !contains(tokens, "<Thickness x:Key=\"TransferContentPadding\">8,0,8,8</Thickness>") ||
        !contains(xaml, "Height=\"{StaticResource CaptionRowHeight}\"") ||
        !contains(xaml, "<RowDefinition x:Name=\"CaptionRowDefinition\" Height=\"{StaticResource CaptionRowGridLength}\" />") ||
        contains(xaml, "ComfortableState") || contains(tokens, "QueueMaxHeightComfortable")) {
        return fail(12, "compact resources must be live, shared and free of unreachable width states");
    }

    for (const auto* key : {"CaptionRowHeight", "TelemetrySpeedMinWidth", "TelemetryPercentMinWidth"}) {
        if (contains(xaml, std::string("Definition Height=\"{StaticResource ") + key) ||
            contains(xaml, std::string("Definition Width=\"{StaticResource ") + key)) {
            return fail(30, "Double token used on a GridLength property");
        }
    }

    // Step 3a design-system invariants: runtime title height follows AppWindow
    // (with a 32 epx fallback), controls use the 4 epx grid, and typography/icons
    // stay on the Windows scale. Visual validation remains a separate gate.
    if (!contains(window, "title_bar.Height() * 96.0 / static_cast<double>(dpi)") ||
        !contains(window, "CaptionRowDefinition().Height") ||
        contains(tokens, "CompactSurfaceHeight") ||
        contains(tokens, "ProgressFillOpacity") ||
        !contains(tokens, "<x:Double x:Key=\"ActionButtonSize\">32</x:Double>") ||
        contains(tokens, "SurfaceActionButtonSize") || contains(tokens, "QueueCommandButtonSize") ||
        !contains(tokens, "<x:Double x:Key=\"ActionIconSize\">16</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"CaptionFontSize\">12</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"BodyFontSize\">14</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"SubtitleFontSize\">20</x:Double>") ||
        contains(tokens, "CancelIconSize") || contains(tokens, "DisclosureIconSize") ||
        contains(tokens, "TelemetrySecondaryOpacity") || contains(tokens, "TelemetryEmphasisOpacity") ||
        contains(tokens, "QueueCountOpacity") || contains(tokens, "QueueItemLocationOpacity") ||
        contains(tokens, "QueueMaxHeightCompact") ||
        !contains(xaml, "TextFillColorSecondaryBrush") || !contains(xaml, "TextFillColorTertiaryBrush")) {
        return fail(17, "step 3a design-system invariants must remain normalized and runtime-aware");
    }

    if (!contains(execution, "if (SpeedText().Text() != speed)") ||
        !contains(execution, "if (EtaText().Text() != eta)") ||
        !contains(execution, "if (CurrentItemText().Text() != filename)") ||
        !contains(execution, "TransferBytesText().Text(bytes_text)") ||
        !contains(execution, "completed of {}") ||
        !contains(execution, "SourcePathText().Text(source)") ||
        !contains(execution, "DestinationPathText().Text(destination)") ||
        !contains(execution, "ToolTipService::SetToolTip(SourcePathText()") ||
        !contains(execution, "ToolTipService::SetToolTip(DestinationPathText()")) {
        return fail(13, "telemetry must avoid redundant text/layout invalidation");
    }

    if (!contains(window, "GiB/s") || !contains(window, "KiB/s") ||
        !contains(window, "{} h {:02} m")) {
        return fail(14, "compact telemetry formatting must scale speed and represent multi-hour ETA compactly");
    }

    if (!contains(body_of(execution, "void MainWindow::StartTransfer("), "ResetTransferSurface();") ||
        !contains(body_of(menu, "bool MainWindow::StartCopyPlan("), "ResetTransferSurface();") ||
        !contains(window, "void MainWindow::ResetTransferSurface()") ||
        !contains(body_of(window, "void MainWindow::ResetTransferSurface()"), "ErrorBar().IsOpen(false)") ||
        !contains(body_of(window, "void MainWindow::ResetTransferSurface()"), "ErrorBar().Message(L\"\")")) {
        return fail(16, "every new transfer session must clear the previous error surface");
    }

    if (!contains(spec, "Superseded phase-0 baseline") ||
        !contains(spec, "native Windows caption cluster visible") ||
        !contains(spec, "telemetry on the left") ||
        !contains(spec, "actions on the right")) {
        return fail(15, "UI specification must lock the compact native-caption composition");
    }

    // Disabled buttons do not participate in pointer hit-testing. Keep the tooltip
    // on an enabled transparent host so Pause/Cancel remain discoverable while idle.
    if (!contains(xaml, "x:Name=\"PauseButtonHost\"") ||
        !contains(xaml, "x:Name=\"CancelButtonHost\"") ||
        !contains(xaml, "x:Name=\"PrimaryActionCluster\"") ||
        !contains(xaml, "x:Name=\"DetailsButton\"\n                                Grid.Column=\"3\"") ||
        contains(body_of(xaml, "<Button x:Name=\"OptionsButton\""), "Background=\"Transparent\"") ||
        contains(body_of(xaml, "<Button x:Name=\"QueueMoveUpButton\""), "BorderThickness=\"0\"") ||
        contains(body_of(xaml, "<Button x:Name=\"QueueMoveDownButton\""), "BorderThickness=\"0\"") ||
        contains(body_of(xaml, "<Button x:Name=\"QueueRemoveButton\""), "BorderThickness=\"0\"") ||
        !contains(window, "ToolTipService::SetToolTip(PauseButtonHost()") ||
        !contains(window, "ToolTipService::SetToolTip(CancelButtonHost()") ||
        contains(window, "ToolTipService::SetToolTip(PauseButton()") ||
        contains(window, "ToolTipService::SetToolTip(CancelButton()") ||
        contains(execution, "ToolTipService::SetToolTip(PauseButton()") ||
        !contains(execution, "ToolTipService::SetToolTip(PauseButtonHost()")) {
        return fail(31, "disabled primary actions must expose tooltips through enabled hit-test hosts");
    }

    const auto finish_copy = body_of(execution, "void MainWindow::FinishCopy(");
    if (!contains(finish_copy, "result.outcomes.copied_source_retained != 0") ||
        !contains(finish_copy, "StatusCompletedWithIssues") ||
        !contains(finish_copy, "ShowNotice(") ||
        !contains(finish_copy, "if (completed_with_issues)") ||
        !contains(finish_copy, "DestroyCompletedWindow()")) {
        return fail(32, "failed, skipped and retained-source outcomes must produce a visible non-clean terminal state");
    }

    // Auxiliary decision/about surfaces must remain real top-level windows. They
    // may be modal-owned for activation, but must never be positioned as content
    // inside or relative to the transfer surface.
    if (contains(conflict, "TDF_POSITION_RELATIVE_TO_WINDOW") ||
        contains(about, "GWLP_HWNDPARENT") ||
        contains(about, "GetWindowRect(hwnd_") ||
        contains(about, "MessageBoxW(hwnd_")) {
        return fail(33, "auxiliary dialogs/windows must not be embedded or positioned relative to the compact surface");
    }

    const auto initial_position = body_of(window, "void MainWindow::PositionInitialWindow() noexcept");
    if (initial_position.empty() ||
        !contains(initial_position, "MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST)") ||
        !contains(initial_position, "GetMonitorInfoW") ||
        !contains(initial_position, "monitor_info.rcWork") ||
        !contains(initial_position, "GetWindowRect(hwnd_") ||
        !contains(initial_position, "AppWindow().Move(") ||
        count_occurrences(window, "PositionInitialWindow();") != 1) {
        return fail(36, "initial compact-window placement must derive from the monitor work area exactly once");
    }

    if (!contains(xaml, "QueueMoveUpButtonHost") || !contains(xaml, "QueueMoveDownButtonHost") ||
        !contains(xaml, "QueueRemoveButtonHost") ||
        !contains(window, "ToolTipService::SetToolTip(QueueMoveUpButtonHost()") ||
        !contains(window, "ToolTipService::SetToolTip(QueueMoveDownButtonHost()") ||
        !contains(window, "ToolTipService::SetToolTip(QueueRemoveButtonHost()")) {
        return fail(38, "disabled queue commands must expose tooltips through enabled host elements");
    }
    const auto show_notice = body_of(window, "void MainWindow::ShowNotice(");
    const auto show_error = body_of(window, "void MainWindow::ShowError(");
    // The notice row of RootGrid must never be covered by a spanning element. The only RowSpan="2" allowed
    // is the transfer icon inside the transfer header's own two-row grid.
    const auto row_span_count = count_occurrences(xaml, "Grid.RowSpan=\"2\"");
    const auto icon_at = xaml.find("x:Name=\"CurrentItemIcon\"");
    const auto icon_span_at = icon_at == std::string::npos ? std::string::npos : xaml.find("Grid.RowSpan=\"2\"", icon_at);
    const bool row_span_only_on_icon = row_span_count == 0 ||
        (row_span_count == 1 && icon_span_at != std::string::npos && icon_span_at - icon_at < 160);
    if (!row_span_only_on_icon ||
        !contains(execution, "InfoBarSeverity::Warning") ||
        !contains(window, "notice_height") ||
        !contains(show_notice, "ResizeWindowToContent();")) {
        return fail(39, "terminal issue notices must distinguish skip-only warnings and reserve layout space");
    }

    const auto resize_to_content = body_of(window, "void MainWindow::ResizeWindowToContent(");
    const auto collapsed_if = resize_to_content.find("if (!expanded_)");
    const auto collapsed_open = collapsed_if == std::string::npos ? std::string::npos : resize_to_content.find('{', collapsed_if);
    const auto collapsed_branch = brace_body_at(resize_to_content, collapsed_open);
    const auto mode_if = resize_to_content.find("if (expanded_layout_mode_ == ExpandedLayoutMode::ThreeColumn)");
    const auto mode_open = mode_if == std::string::npos ? std::string::npos : resize_to_content.find('{', mode_if);
    const auto three_column_branch = brace_body_at(resize_to_content, mode_open);
    const auto narrow_else = mode_open == std::string::npos ? std::string::npos : resize_to_content.find("else", mode_open + three_column_branch.size());
    const auto narrow_open = narrow_else == std::string::npos ? std::string::npos : resize_to_content.find('{', narrow_else);
    const auto narrow_branch = brace_body_at(resize_to_content, narrow_open);
    if (contains(resize_to_content, "RootGrid().ActualWidth()") ||
        !contains(resize_to_content, "work_width_epx") ||
        !contains(resize_to_content, "ExpandedPreferredWidth") ||
        !contains(resize_to_content, "ExpandedThreeColumnThreshold") ||
        !contains(resize_to_content, "expanded_layout_mode_ = effective_width >= three_column_threshold") ||
        !contains(resize_to_content, "PerformancePanel().Measure(") ||
        !contains(resize_to_content, "InformationPanel().Measure(") ||
        !contains(resize_to_content, "ExpandedRegion().Padding()") ||
        !contains(resize_to_content, "ExpandedColumnSpacing") ||
        !contains(resize_to_content, "expanded_padding.Top + expanded_padding.Bottom") ||
        !contains(three_column_branch, "ExpandedRegion().ColumnSpacing(") ||
        !contains(three_column_branch, "ExpandedColumnSpacing") ||
        !contains(three_column_branch, "token_thickness(L\"ExpandedSectionMargin\"") ||
        !contains(three_column_branch, "Grid::SetRow(DetailsViewport(), 0)") ||
        !contains(three_column_branch, "Grid::SetColumn(DetailsViewport(), 1)") ||
        !contains(three_column_branch, "ExpandedViewport().VerticalScrollMode(ScrollMode::Disabled)") ||
        !contains(three_column_branch, "ExpandedViewport().VerticalScrollBarVisibility(ScrollBarVisibility::Disabled)") ||
        !contains(three_column_branch, "ExpandedViewport().IsTabStop(false)") ||
        !contains(narrow_branch, "ExpandedRegion().ColumnSpacing(0.0)") ||
        !contains(narrow_branch, "PerformancePanel().Margin(") ||
        !contains(xaml, "x:Name=\"ExpandedViewport\"") ||
        !contains(xaml, "MinHeight=\"{StaticResource TransferProgressHeight}\"") ||
        !contains(xaml, "<x:Double x:Key=\"ProgressBarTrackHeight\">8</x:Double>") ||
        contains(xaml, "<ControlTemplate TargetType=\"ProgressBar\"") ||
        !contains(xaml, "Margin=\"{StaticResource TransferProgressMargin}\"") ||
        !contains(xaml, "Background=\"{ThemeResource CardBackgroundFillColorDefaultBrush}\"") ||
        count_occurrences(xaml, "BorderBrush=\"{ThemeResource CardStrokeColorDefaultBrush}\"") < 4 ||
        count_occurrences(xaml, "CornerRadius=\"{ThemeResource ControlCornerRadius}\"") < 4 ||
        !contains(narrow_branch, "Grid::SetRow(DetailsViewport(), 1)") ||
        !contains(narrow_branch, "Grid::SetColumn(DetailsViewport(), 0)") ||
        !contains(narrow_branch, "ExpandedViewport().VerticalScrollMode(ScrollMode::Auto)") ||
        !contains(narrow_branch, "ExpandedViewport().VerticalScrollBarVisibility(ScrollBarVisibility::Auto)") ||
        !contains(resize_to_content, "ExpandedViewport().MaxHeight(expanded_height_cap)") ||
        !contains(resize_to_content, "DetailsExpandedMinHeight") ||
        !contains(resize_to_content, "const double required_content_height = queue_min_height + details_min_height;") ||
        !contains(resize_to_content, "const bool constrained_height = content_cap < required_content_height;") ||
        !contains(resize_to_content, "ExpandedViewport().IsTabStop(constrained_height)") ||
        !contains(resize_to_content, "const double queue_height = queue_min_height;") ||
        !contains(resize_to_content, "? details_min_height") ||
        !contains(resize_to_content, "expanded_region_height = constrained_height") ||
        !contains(resize_to_content, "? expanded_height_cap") ||
        contains(resize_to_content, "fixed_content_height") ||
        contains(resize_to_content, "(std::min)(queue_min_height, content_cap - fixed_content_height)") ||
        contains(resize_to_content, "QueuePanel().Measure(") ||
        !contains(resize_to_content, "Grid::SetRow(QueuePanel()") ||
        !contains(resize_to_content, "Grid::SetColumn(InformationPanel()") ||
        !contains(resize_to_content, "ResizeWindow(") ||
        contains(window, "QueueList().MaxHeight(") ||
        contains(window, "normal_surface_fallback") ||
        !contains(xaml, "<RowDefinition Height=\"*\" />")) {
        return fail(43, "expanded layout must select composition before target-width measurement, exclude Queue from infinite-height measurement, and remove the legacy queue MaxHeight");
    }
    const auto performance_graph = body_of(window, "void MainWindow::UpdatePerformanceGraph()");
    if (!contains(xaml, "x:Name=\"PerformanceGraphArea\"") ||
        !contains(xaml, "x:Name=\"PerformanceGraphLine\"") ||
        !contains(xaml, "Stroke=\"{ThemeResource AccentFillColorDefaultBrush}\"") ||
        !contains(xaml, "Fill=\"{ThemeResource AccentFillColorDefaultBrush}\"") ||
        !std::regex_search(xaml, std::regex{R"(<TextBlock[^>]*x:Name="PerformanceScaleMaxText"[^>]*AutomationProperties\.AccessibilityView="Raw"[^>]*/>)"}) ||
        !std::regex_search(xaml, std::regex{R"(<TextBlock[^>]*x:Name="PerformanceScaleMidText"[^>]*AutomationProperties\.AccessibilityView="Raw"[^>]*/>)"}) ||
        !std::regex_search(xaml, std::regex{R"(<TextBlock[^>]*x:Name="PerformanceScaleZeroText"[^>]*AutomationProperties\.AccessibilityView="Raw"[^>]*/>)"}) ||
        !std::regex_search(xaml, std::regex{R"(<Polygon[^>]*x:Name="PerformanceGraphArea"[^>]*AutomationProperties\.AccessibilityView="Raw"[^>]*/>)"}) ||
        !std::regex_search(xaml, std::regex{R"(<Polyline[^>]*x:Name="PerformanceGraphLine"[^>]*AutomationProperties\.AccessibilityView="Raw"[^>]*/>)"}) ||
        contains(xaml, "PerformanceGraphBrushSource") ||
        contains(performance_graph, "Border bar") ||
        contains(performance_graph, "Children().") ||
        !contains(performance_graph, "performance_speed_samples_.size() < 2") ||
        !contains(performance_graph, "sample_capacity = 60.0") ||
        !contains(performance_graph, "double peak = 0.0") ||
        !contains(performance_graph, "velocitycopy::ui::performance_scale(peak)") ||
        !contains(performance_graph, "scale.ceiling_bytes_per_second") ||
        !contains(performance_graph, "FormatPerformanceScaleSpeed(scale_max, scale.unit_bytes)") ||
        !contains(performance_graph, "FormatPerformanceScaleSpeed(scale_max / 2.0, scale.unit_bytes)") ||
        !contains(performance_graph, "FormatPerformanceScaleSpeed(0.0, scale.unit_bytes)") ||
        !contains(performance_graph, "PerformanceGraphLine().StrokeThickness()") ||
        !contains(window, "UpdatePerformanceAxisWidth()") ||
        !contains(window, "label.Text(L\"1000 MiB/s\")") ||
        !contains(window, "label.DesiredSize().Width") ||
        !contains(window, "PerformanceScaleLabels().Margin()") ||
        !contains(window, "axis_margin.Left + axis_margin.Right") ||
        !contains(window, "PerformanceScaleColumn().MinWidth(") ||
        !contains(window, "token_double(L\"PerformanceAxisLabelMinWidth\"") ||
        !contains(performance_graph, "for (const double sample : performance_speed_samples_)") ||
        !contains(performance_graph, "performance_speed_samples_.size() - 1") ||
        !contains(performance_graph, "width - slot * static_cast<double>") ||
        !contains(performance_graph, "PerformanceGraphLine().Points(line_points)") ||
        !contains(performance_graph, "PerformanceGraphArea().Points(area_points)")) {
        return fail(44, "performance graph must use the phase 2 sample history as a declarative right-anchored line/area plot with a binary-unit nice Y scale");
    }

    const auto resize_window = body_of(window, "void MainWindow::ResizeWindow(");
    if (!contains(resize_window, "client_width_epx") ||
        !contains(resize_window, "client_height_epx") ||
        !contains(resize_window, "MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)") ||
        !contains(resize_window, "monitor_info.rcWork") ||
        !contains(resize_window, "ExpandedWorkAreaMargin") ||
        !contains(resize_window, "if (!preserve_position)") ||
        !contains(resize_window, "x + window_width > monitor_info.rcWork.right") ||
        !contains(resize_window, "y + window_height > monitor_info.rcWork.bottom") ||
        !contains(resize_window, "reposition ? 0 : SWP_NOMOVE")) {
        return fail(42, "phase 3 resize must own width/height, preserve in-bounds position, suppress DPI repositioning, and correct only work-area overflow");
    }
    if (!contains(show_notice, "ErrorBar().Severity(severity)") ||
        !contains(show_notice, "ErrorBar().IsOpen(true)") ||
        count_occurrences(show_notice, "ResizeWindowToContent();") != 2 ||
        !contains(show_notice, "dispatcher_.TryEnqueue") ||
        !contains(show_notice, "self->ErrorBar().IsOpen()") ||
        !contains(show_error, "ShowNotice(InfoBarSeverity::Error, title, message)") ||
        !contains(window, "ErrorBar().Closed(") ||
        !contains(window, "self->ResizeWindowToContent();") ||
        !contains(resize_to_content, "QueueExpandedMinHeight") ||
        !contains(collapsed_branch, "normal_height + notice_height") ||
        !contains(resize_to_content, "normal_height + notice_height + expanded_region_height") ||
        !contains(resize_to_content, "work_height_epx - work_margin * 2.0 - normal_height - notice_height") ||
        count_occurrences(execution, "ResizeWindow(velocitycopy::ui::token_int(L\"CompactSurfaceHeight\"") != 0) {
        return fail(40, "notice and terminal paths must resize from measured content");
    }

    if (!contains(resize_window, "GetWindowRect(hwnd, &window_rect)") ||
        !contains(resize_window, "GetClientRect(hwnd, &client_rect)") ||
        !contains(resize_window, "client_width + frame_width") ||
        !contains(resize_window, "client_height + frame_height") ||
        !contains(resize_window, "resize_in_progress_ = true") ||
        !contains(resize_to_content, "if (resize_in_progress_) return") ||
        !contains(window, "root.RasterizationScale()") ||
        !contains(window, "scale - window->last_rasterization_scale_") ||
        !contains(window, "window->ResizeWindowToContent(true)") ||
        !contains(window, "ui_settings_ = Windows::UI::ViewManagement::UISettings()") ||
        !contains(window, "ui_settings_.TextScaleFactorChanged(auto_revoke") ||
        !contains(window, "scale - window->last_text_scale_factor_") ||
        !contains(resize_to_content, "RootGrid().UpdateLayout()") ||
        !contains(resize_to_content, "TransferSurface().Measure({measure_width, std::numeric_limits<float>::infinity()})") ||
        !contains(resize_to_content, "TransferSurface().DesiredSize().Height")) {
        return fail(42, "content sizing must compensate the native frame and guard DPI/text-scale remeasurement from self-resize loops");
    }

    if (contains(finish_copy, "ErrorBar().Severity(") ||
        !contains(finish_copy, "ShowNotice(") ||
        !contains(finish_copy, "? InfoBarSeverity::Warning") ||
        !contains(show_error, "ShowNotice(InfoBarSeverity::Error, message)") ||
        count_occurrences(window, "ErrorBar().Severity(") != 1) {
        return fail(41, "notice severity must be explicit per message and ordinary errors must always use Error severity");
    }


    const auto set_expanded = body_of(window, "void MainWindow::SetExpanded(");
    const auto details_click = body_of(window, "void MainWindow::OnDetailsClick(");
    const auto observe_performance = body_of(window, "void MainWindow::ObservePerformanceSample(");
    const auto update_performance = body_of(window, "void MainWindow::UpdatePerformanceGraph()");
    if (!contains(xaml, "x:Name=\"PerformanceGraph\"") ||
        !contains(details_click, "SetExpanded(!expanded_)") ||
        contains(details_click, "ResizeWindowToContent();") ||
        !contains(set_expanded, "ResizeWindowToContent();") ||
        contains(set_expanded, "Transitional Phase 2") ||
        contains(execution, "SetExpanded(false);\n    ResizeWindowToContent();") ||
        contains(queue_persistence, "SetExpanded(false);\n    ResizeWindowToContent();") ||
        contains(conflict, "SetExpanded(false);\n    ResizeWindowToContent();") ||
        contains(xaml, "x:Name=\"QueueButton\"") ||
        stray_queue_button_reference ||
        !contains(xaml, "x:Name=\"ExpandedRegion\"") ||
        !contains(xaml, "x:Name=\"PerformancePanel\"") ||
        !contains(xaml, "x:Name=\"InformationPanel\"") ||
        count_occurrences(window, "ExpandedRegion().Visibility(") != 1 ||
        contains(window, "QueuePanel().Visibility(") ||
        contains(window, "DetailsPanel().Visibility(") ||
        !contains(window, "void MainWindow::SetExpanded(") ||
        stray_expanded_visibility_writer ||
        !contains(observe_performance, "performance_sampling_state_ != PerformanceSamplingState::Copying") ||
        !contains(observe_performance, "now - last_performance_sample_ms_ < 500") ||
        !contains(observe_performance, "performance_speed_samples_.size() > 60") ||
        contains(update_performance, "Application::Current().Resources().Lookup") ||
        !contains(update_performance, "OutputDebugStringW") ||
        contains(update_performance, "executor_") ||
        contains(update_performance, "presenter_") ||
        !contains(execution, "performance_sampling_state_ = PerformanceSamplingState::Cancelling") ||
        !contains(execution, "performance_sampling_state_ = PerformanceSamplingState::Paused") ||
        !contains(execution, "performance_sampling_state_ = PerformanceSamplingState::Copying")) {
        return fail(45, "phase 2 sampling and expanded presentation contracts must remain bounded, presentation-only, and separate from Queue execution semantics");
    }

    return 0;
}