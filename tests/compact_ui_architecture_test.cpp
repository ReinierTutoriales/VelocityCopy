#include "architecture_support.hpp"

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
    const auto window = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tokens = read_source(root / "src/ui/DesignTokens.xaml");
    const auto spec = read_source(root / "docs/UI_SPEC.md");
    const auto conflict = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Conflict.cpp");
    const auto about = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.About.cpp");

    if (xaml.empty() || header.empty() || execution.empty() || queue.empty() ||
        menu.empty() || window.empty() || tokens.empty() || spec.empty() || conflict.empty() || about.empty()) {
        return fail(1, "required UI source missing");
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
        !contains(xaml, "x:Name=\"QueueButton\"")) {
        return fail(2, "collapsed surface must separate telemetry from the right-aligned primary actions");
    }

    const auto caption_start = xaml.find("x:Name=\"CaptionContentGrid\"");
    const auto bottom_start = xaml.find("x:Name=\"BottomContentGrid\"");
    const auto speed_start = xaml.find("x:Name=\"SpeedText\"");
    if (caption_start == std::string::npos || bottom_start == std::string::npos || speed_start == std::string::npos ||
        speed_start < bottom_start || (bottom_start > caption_start && contains(xaml.substr(caption_start, bottom_start - caption_start), "SpeedText"))) {
        return fail(3, "telemetry must not share the caption-constrained filename row");
    }

    // UI_SPEC: the compact surface has exactly four primary actions (Pause/Resume,
    // Cancel, Options, queue disclosure). Skip and Stop are Options menu commands
    // only, with dynamic enablement; no visible or hidden XAML buttons.
    const auto refresh_menu = body_of(menu, "void MainWindow::RefreshExecutionMenuState(");
    const auto cluster_start = xaml.find("x:Name=\"PrimaryActionCluster\"");
    const auto cluster_end = cluster_start == std::string::npos ? std::string::npos
                                                                : xaml.find("</StackPanel>", cluster_start);
    const auto primary_action_count = cluster_end == std::string::npos
        ? 0
        : count_occurrences(xaml.substr(cluster_start, cluster_end - cluster_start), "<Button");
    if (contains(xaml, "SkipButton") || contains(xaml, "StopButton") ||
        contains(xaml, "OnSkipClick") || contains(xaml, "OnStopClick") ||
        contains(execution, "SkipButton()") || contains(execution, "StopButton()") ||
        contains(window, "SkipButton()") || contains(window, "StopButton()") ||
        contains(header, "RefreshExecutionButtonState") ||
        contains(tokens, "SkipIconSize") || contains(tokens, "StopIconSize") ||
        primary_action_count != 5 ||
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
    if (glyph_of("QueueButton") != "&#xE70D;" ||
        glyph_of("QueueMoveUpButton") != "&#xE74A;" ||
        glyph_of("QueueMoveDownButton") != "&#xE74B;" ||
        glyph_of("QueueRemoveButton") != "&#xE738;" ||
        glyph_of("CancelButton") != "&#xE71A;" ||
        contains(xaml, "&#xE74D;") || contains(xaml, "&#xE711;") || contains(xaml, "&#xE8BB;")) {
        return fail(16, "action glyphs must be unambiguous and match their command semantics");
    }

    if (!contains(xaml, "<ProgressBar x:Name=\"TransferProgress\"") ||
        contains(xaml, "x:Name=\"ProgressFill\"") ||
        !contains(window, "TransferProgress().Value(percent)") ||
        contains(window, "ProgressFill().Width(")) {
        return fail(5, "compact surface must use one native progress indicator driven by the centralized progress fraction");
    }

    // DesignTokens.xaml is the single width source; C++ reads it through the
    // UiTokens.h accessor with an identical fallback.
    if (!contains(tokens, "<x:Double x:Key=\"NormalWindowMinWidth\">380</x:Double>") ||
        !contains(window, "token_int(L\"NormalWindowMinWidth\", 380)") ||
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
        !contains(tokens, "<Thickness x:Key=\"QueuePanelPadding\">8,8,8,12</Thickness>") ||
        !contains(xaml, "Height=\"{StaticResource CaptionRowHeight}\"") ||
        !contains(xaml, "<RowDefinition x:Name=\"CaptionRowDefinition\" Height=\"{StaticResource CaptionRowGridLength}\" />") ||
        !contains(xaml, "Padding=\"{StaticResource QueuePanelPadding}\"") ||
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
        !contains(xaml, "TextFillColorSecondaryBrush") || !contains(xaml, "TextFillColorTertiaryBrush") ||
        !contains(window, "queue_ceiling - normal_surface_fallback")) {
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
    if (contains(xaml, "Grid.RowSpan=\"2\"") ||
        !contains(execution, "InfoBarSeverity::Warning") ||
        !contains(window, "notice_height") ||
        !contains(show_notice, "ResizeWindowToContent();")) {
        return fail(39, "terminal issue notices must distinguish skip-only warnings and reserve layout space");
    }

    const auto resize_to_content = body_of(window, "void MainWindow::ResizeWindowToContent()");
    const auto resize_window = body_of(window, "void MainWindow::ResizeWindow(");
    if (!contains(show_notice, "ErrorBar().Severity(severity)") ||
        !contains(show_notice, "ErrorBar().IsOpen(true)") ||
        count_occurrences(show_notice, "ResizeWindowToContent();") != 2 ||
        !contains(show_notice, "dispatcher_.TryEnqueue") ||
        !contains(show_notice, "self->ErrorBar().IsOpen()") ||
        !contains(show_error, "ShowNotice(InfoBarSeverity::Error, message)") ||
        !contains(window, "ErrorBar().Closed(") ||
        !contains(window, "self->ResizeWindowToContent();") ||
        !contains(resize_to_content, "QueueExpandedMinHeight") ||
        !contains(resize_to_content, "+ notice_height_epx") ||
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
        !contains(window, "ui_settings_ = Windows::UI::ViewManagement::UISettings()") ||
        !contains(window, "ui_settings_.TextScaleFactorChanged(auto_revoke") ||
        !contains(window, "scale - window->last_text_scale_factor_") ||
        !contains(resize_to_content, "RootGrid().UpdateLayout()") ||
        !contains(resize_to_content, "TransferSurface().Measure({measured_width, std::numeric_limits<float>::infinity()})") ||
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


    const auto queue_click = body_of(queue, "void MainWindow::OnQueueClick(");
    const auto details_click = body_of(window, "void MainWindow::OnDetailsClick(");
    const auto observe_performance = body_of(window, "void MainWindow::ObservePerformanceSample(");
    const auto update_performance = body_of(window, "void MainWindow::UpdatePerformanceGraph()");
    if (!contains(xaml, "x:Name=\"DetailsPanel\"") ||
        !contains(xaml, "x:Name=\"PerformanceGraph\"") ||
        !contains(xaml, "x:Name=\"PerformanceGraph\" AutomationProperties.AccessibilityView=\"Raw\"") ||
        !contains(details_click, "QueuePanel().Visibility(Visibility::Collapsed)") ||
        !contains(queue_click, "SetDetailsExpanded(false)") ||
        !contains(observe_performance, "performance_sampling_state_ != PerformanceSamplingState::Copying") ||
        !contains(observe_performance, "now - last_performance_sample_ms_ < 500") ||
        !contains(observe_performance, "performance_speed_samples_.size() > 60") ||
        !contains(update_performance, "double peak = 1.0") ||
        !contains(update_performance, "sample <= 0.0 ? 1.0") ||
        !contains(update_performance, "PerformanceGraphBrushSource().Background()") ||
        contains(update_performance, "Application::Current().Resources().Lookup") ||
        !contains(update_performance, "OutputDebugStringW") ||
        contains(update_performance, "executor_") ||
        contains(update_performance, "presenter_") ||
        !contains(execution, "performance_sampling_state_ = PerformanceSamplingState::Cancelling") ||
        !contains(execution, "performance_sampling_state_ = PerformanceSamplingState::Paused") ||
        !contains(execution, "performance_sampling_state_ = PerformanceSamplingState::Copying")) {
        return fail(43, "phase 2 details must remain separate from Queue and keep bounded presentation-only performance history");
    }

    return 0;
}