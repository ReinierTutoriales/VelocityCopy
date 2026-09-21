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
        !contains(xaml, "x:Name=\"PauseButton\"") ||
        !contains(xaml, "x:Name=\"CancelButton\"") ||
        !contains(xaml, "x:Name=\"OptionsButton\"") ||
        !contains(xaml, "Glyph=\"&#xE712;\"") ||
        !contains(xaml, "x:Name=\"QueueButton\"")) {
        return fail(2, "collapsed surface must expose only primary transfer actions");
    }

    if (!contains(xaml, "x:Name=\"SkipButton\"") ||
        !contains(xaml, "x:Name=\"StopButton\"") ||
        xaml.find("x:Name=\"SkipButton\"") > xaml.find("Visibility=\"Collapsed\"", xaml.find("x:Name=\"SkipButton\"")) ||
        xaml.find("x:Name=\"StopButton\"") > xaml.find("Visibility=\"Collapsed\"", xaml.find("x:Name=\"StopButton\""))) {
        return fail(3, "Skip and Stop must remain non-visual command accessors, not visible row controls");
    }

    if (contains(xaml, "<ProgressBar") || contains(xaml, "x:Name=\"ActionStrip\"") ||
        !contains(window, "ProgressFill().Width(TransferSurface().ActualWidth() * progress_fraction_)")) {
        return fail(4, "window surface itself must remain the only progress indicator");
    }

    if (!contains(tokens, "<x:Double x:Key=\"WindowCompactWidth\">360</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"WindowMinWidth\">360</x:Double>") ||
        !contains(window, "constexpr int kCompactWindowWidthEpx = 360")) {
        return fail(5, "compact geometry must remain 360 epx wide");
    }

    if (!contains(window, "presenter.SetBorderAndTitleBar(true, false)") ||
        !contains(window, "presenter.IsMinimizable(false)") ||
        !contains(window, "presenter.IsMaximizable(false)") ||
        !contains(window, "presenter.IsResizable(false)") ||
        contains(window, "RightInset()") || contains(window, "ApplyTitleBarInset") ||
        contains(window, "OnAppWindowChanged") || contains(header, "ApplyTitleBarInset") ||
        contains(header, "OnAppWindowChanged") || contains(header, "base_transfer_content_padding_")) {
        return fail(6, "system caption controls must stay removed without legacy inset machinery");
    }

    if (!contains(xaml, "x:Name=\"TitleBarDragRegion\"") ||
        !contains(window, "SetTitleBar(TitleBarDragRegion())") ||
        !contains(xaml, "AllowDrop=\"True\"") || !contains(xaml, "Drop=\"OnDrop\"")) {
        return fail(7, "custom drag region and whole-window append drop must remain wired");
    }

    if (!contains(menu, "skip_menu_item_.Text") || !contains(menu, "stop_menu_item_.Text") ||
        !contains(menu, "ActionHideToTray") || !contains(menu, "HideToTray()") ||
        !contains(menu, "skip_menu_item_.Click({this, &MainWindow::OnSkipClick})") ||
        !contains(menu, "stop_menu_item_.Click({this, &MainWindow::OnStopClick})")) {
        return fail(8, "secondary transfer commands and hide-to-tray must live in Options");
    }

    if (!contains(xaml, "Padding=\"{StaticResource TransferContentPadding}\"") ||
        !contains(xaml, "Margin=\"{StaticResource BrandToContentMargin}\"") ||
        !contains(xaml, "Margin=\"{StaticResource TransportLeadMargin}\"") ||
        !contains(xaml, "Margin=\"{StaticResource InlineControlMargin}\"") ||
        !contains(queue, "row.Margin(Thickness{8, 4, 8, 4})")) {
        return fail(9, "compact spacing must stay tokenized and queue rows aligned to the 4/8 rhythm");
    }

    if (!contains(spec, "360 × 72 epx") || !contains(spec, "system caption buttons are removed") ||
        !contains(spec, "Skip and Stop live in Options")) {
        return fail(10, "UI specification must lock the compact chrome-free composition");
    }

    return 0;
}
