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

    if (xaml.empty() || header.empty() || execution.empty() || queue.empty() ||
        menu.empty() || window.empty() || tokens.empty() || spec.empty()) {
        return fail(1, "required UI source missing");
    }

    if (!contains(xaml, "x:Name=\"TransferSurface\"") ||
        !contains(xaml, "x:Name=\"ProgressFill\"") ||
        !contains(xaml, "x:Name=\"BrandLogo\"") ||
        !contains(xaml, "x:Name=\"BottomContentGrid\"") ||
        !contains(xaml, "x:Name=\"TelemetryStrip\"") ||
        !contains(xaml, "x:Name=\"PrimaryActionCluster\"") ||
        !contains(xaml, "HorizontalAlignment=\"Right\"") ||
        !contains(xaml, "x:Name=\"PauseButton\"") ||
        !contains(xaml, "x:Name=\"CancelButton\"") ||
        !contains(xaml, "x:Name=\"OptionsButton\"") ||
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
        primary_action_count != 4 ||
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

    if (contains(xaml, "<ProgressBar") ||
        !contains(window, "ProgressFill().Width(TransferSurface().ActualWidth() * progress_fraction_)")) {
        return fail(5, "window surface itself must remain the only progress indicator");
    }

    // DesignTokens.xaml is the single width source; C++ reads it through the
    // UiTokens.h accessor with an identical fallback.
    if (!contains(tokens, "<x:Double x:Key=\"CompactWindowWidth\">380</x:Double>") ||
        !contains(window, "token_int(L\"CompactWindowWidth\", 380)") ||
        contains(window, "kCompactWindowWidthEpx") ||
        contains(tokens, "WindowCompactWidth") || contains(tokens, "WindowMinWidth") ||
        contains(tokens, "WindowComfortableBreakpoint")) {
        return fail(6, "compact geometry must have one runtime width source instead of dead resource mirrors");
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
        !contains(xaml, "AllowDrop=\"True\"") || !contains(xaml, "Drop=\"OnDrop\"")) {
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
        !contains(tokens, "<x:Double x:Key=\"CompactSurfaceHeight\">72</x:Double>") ||
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
        !contains(window, "queue_ceiling - compact_height")) {
        return fail(17, "step 3a design-system invariants must remain normalized and runtime-aware");
    }

    if (!contains(execution, "if (SpeedText().Text() != speed)") ||
        !contains(execution, "if (EtaText().Text() != eta)") ||
        !contains(execution, "if (CurrentItemText().Text() != filename)")) {
        return fail(13, "telemetry must avoid redundant text/layout invalidation");
    }

    if (!contains(window, "GiB/s") || !contains(window, "KiB/s") ||
        !contains(window, "{} h {:02} m")) {
        return fail(14, "compact telemetry formatting must scale speed and represent multi-hour ETA compactly");
    }

    if (!contains(body_of(execution, "void MainWindow::StartTransfer("), "ResetTransferSurface();") ||
        !contains(body_of(menu, "void MainWindow::StartCopyPlan("), "ResetTransferSurface();") ||
        !contains(window, "void MainWindow::ResetTransferSurface()") ||
        !contains(body_of(window, "void MainWindow::ResetTransferSurface()"), "ErrorBar().IsOpen(false)") ||
        !contains(body_of(window, "void MainWindow::ResetTransferSurface()"), "ErrorBar().Message(L\"\")")) {
        return fail(16, "every new transfer session must clear the previous error surface");
    }

    if (!contains(spec, "380 × 72 epx") ||
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
        contains(window, "ToolTipService::SetToolTip(CancelButton()")) {
        return fail(31, "disabled primary actions must expose tooltips through enabled hit-test hosts");
    }

    const auto finish_copy = body_of(execution, "void MainWindow::FinishCopy(");
    if (!contains(finish_copy, "result.outcomes.failed != 0 || result.outcomes.skipped != 0") ||
        !contains(finish_copy, "StatusCompletedWithIssues") ||
        !contains(finish_copy, "ShowError(") ||
        !contains(finish_copy, "if (completed_with_issues)") ||
        !contains(finish_copy, "DestroyCompletedWindow()")) {
        return fail(32, "per-item failed/skipped outcomes must produce a visible non-clean terminal state");
    }

    return 0;
}
