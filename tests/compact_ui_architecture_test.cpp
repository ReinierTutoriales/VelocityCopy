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
        !contains(xaml, "x:Name=\"PrimaryActionCluster\"") ||
        !contains(xaml, "HorizontalAlignment=\"Center\"") ||
        !contains(xaml, "x:Name=\"PauseButton\"") ||
        !contains(xaml, "x:Name=\"CancelButton\"") ||
        !contains(xaml, "x:Name=\"OptionsButton\"") ||
        !contains(xaml, "x:Name=\"QueueButton\"")) {
        return fail(2, "collapsed surface must keep centered primary transfer actions");
    }

    if (!contains(xaml, "x:Name=\"SkipButton\"") ||
        !contains(xaml, "x:Name=\"StopButton\"") ||
        xaml.find("Visibility=\"Collapsed\"", xaml.find("x:Name=\"SkipButton\"")) == std::string::npos ||
        xaml.find("Visibility=\"Collapsed\"", xaml.find("x:Name=\"StopButton\"")) == std::string::npos) {
        return fail(3, "Skip and Stop must remain non-visual command accessors");
    }

    if (contains(xaml, "<ProgressBar") ||
        !contains(window, "ProgressFill().Width(TransferSurface().ActualWidth() * progress_fraction_)")) {
        return fail(4, "window surface itself must remain the only progress indicator");
    }

    if (!contains(tokens, "<x:Double x:Key=\"WindowCompactWidth\">380</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"WindowMinWidth\">360</x:Double>") ||
        !contains(window, "constexpr int kCompactWindowWidthEpx = 380")) {
        return fail(5, "compact geometry must remain 380 epx wide with a 360 epx floor");
    }

    if (!contains(window, "presenter.IsMinimizable(true)") ||
        !contains(window, "presenter.IsMaximizable(false)") ||
        !contains(window, "presenter.IsResizable(false)") ||
        contains(window, "SetBorderAndTitleBar(true, false)")) {
        return fail(6, "native Windows caption buttons must remain visible while resize/maximize stay constrained");
    }

    if (!contains(xaml, "x:Name=\"CaptionContentGrid\"") ||
        !contains(header, "void ApplyTitleBarInset() noexcept") ||
        !contains(header, "base_caption_content_padding_") ||
        !contains(window, "AppWindow().TitleBar().RightInset()") ||
        !contains(window, "CaptionContentGrid().Padding") ||
        contains(window, "TransferContentGrid().Padding(Thickness{")) {
        return fail(7, "caption inset must affect only the top content row, not the centered action row");
    }

    if (!contains(xaml, "x:Name=\"TitleBarDragRegion\"") ||
        !contains(window, "SetTitleBar(TitleBarDragRegion())") ||
        !contains(xaml, "AllowDrop=\"True\"") || !contains(xaml, "Drop=\"OnDrop\"")) {
        return fail(8, "custom drag region and whole-window append drop must remain wired");
    }

    if (!contains(menu, "skip_menu_item_.Text") || !contains(menu, "stop_menu_item_.Text") ||
        !contains(menu, "skip_menu_item_.Click({this, &MainWindow::OnSkipClick})") ||
        !contains(menu, "stop_menu_item_.Click({this, &MainWindow::OnStopClick})")) {
        return fail(9, "secondary transfer commands must remain available in Options");
    }

    if (!contains(tokens, "<Thickness x:Key=\"TransferContentPadding\">8,4,8,4</Thickness>") ||
        !contains(tokens, "<Thickness x:Key=\"CaptionContentPadding\">0</Thickness>") ||
        !contains(xaml, "Padding=\"{StaticResource TransferContentPadding}\"") ||
        !contains(xaml, "Padding=\"{StaticResource CaptionContentPadding}\"") ||
        !contains(queue, "row.Margin(Thickness{8, 4, 8, 4})")) {
        return fail(10, "compact spacing must stay tokenized and queue rows aligned to the 4/8 rhythm");
    }

    if (!contains(spec, "380 × 72 epx") ||
        !contains(spec, "native Windows caption cluster visible") ||
        !contains(spec, "Skip and Stop live in Options")) {
        return fail(11, "UI specification must lock the compact native-caption composition");
    }

    return 0;
}
