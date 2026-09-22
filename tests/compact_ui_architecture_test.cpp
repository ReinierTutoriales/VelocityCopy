#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}

bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}

std::string body_of(const std::string& source, const std::string& signature) {
    const auto start = source.find(signature);
    if (start == std::string::npos) return {};
    const auto end = source.find("\n}\n", start);
    return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

int fail(const int code, const char* message) {
    std::cerr << "compact UI architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto menu = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tokens = read_all(root / "src/ui/DesignTokens.xaml");
    const auto spec = read_all(root / "docs/UI_SPEC.md");

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
        !contains(xaml, "x:Name=\"SkipButton\"") ||
        !contains(xaml, "x:Name=\"StopButton\"") ||
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

    if (!contains(xaml, "Click=\"OnSkipClick\"") || !contains(xaml, "Click=\"OnStopClick\"") ||
        !contains(execution, "SkipButton().IsEnabled") || !contains(execution, "StopButton().IsEnabled") ||
        !contains(window, "ToolTipService::SetToolTip(SkipButton()") ||
        !contains(window, "ToolTipService::SetToolTip(StopButton()")) {
        return fail(4, "skip and stop must remain visible primary transfer controls with live state and tooltips");
    }

    if (contains(xaml, "<ProgressBar") ||
        !contains(window, "ProgressFill().Width(TransferSurface().ActualWidth() * progress_fraction_)")) {
        return fail(5, "window surface itself must remain the only progress indicator");
    }

    if (!contains(window, "constexpr int kCompactWindowWidthEpx = 380") ||
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
        !contains(window, "AppWindow().TitleBar().RightInset()") ||
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

    if (!contains(tokens, "<x:Double x:Key=\"CaptionRowHeight\">34</x:Double>") ||
        !contains(tokens, "<GridLength x:Key=\"CaptionRowGridLength\">34</GridLength>") ||
        !contains(tokens, "<x:Double x:Key=\"TelemetrySpeedMinWidth\">64</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"TelemetryPercentMinWidth\">36</x:Double>") ||
        !contains(tokens, "<Thickness x:Key=\"TransferContentPadding\">8,4,8,4</Thickness>") ||
        !contains(tokens, "<Thickness x:Key=\"QueuePanelPadding\">8,8,8,12</Thickness>") ||
        !contains(xaml, "Height=\"{StaticResource CaptionRowHeight}\"") ||
        !contains(xaml, "<RowDefinition Height=\"{StaticResource CaptionRowGridLength}\" />") ||
        !contains(xaml, "Padding=\"{StaticResource QueuePanelPadding}\"") ||
        contains(xaml, "ComfortableState") || contains(tokens, "QueueMaxHeightComfortable")) {
        return fail(12, "compact resources must be live, shared and free of unreachable width states");
    }

    for (const auto* key : {"CaptionRowHeight", "QueueMaxHeightCompact",
                            "TelemetrySpeedMinWidth", "TelemetryPercentMinWidth"}) {
        if (contains(xaml, std::string("Definition Height=\"{StaticResource ") + key) ||
            contains(xaml, std::string("Definition Width=\"{StaticResource ") + key)) {
            return fail(30, "Double token used on a GridLength property");
        }
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

    return 0;
}
