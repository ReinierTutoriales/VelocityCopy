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
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto copy_append = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");
    const auto shell = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tokens = read_all(root / "src/ui/DesignTokens.xaml");
    const auto spec = read_all(root / "docs/UI_SPEC.md");

    if (xaml.empty() || header.empty() || execution.empty() || queue.empty() || copy_append.empty() || shell.empty() || window.empty() || tokens.empty() || spec.empty()) {
        return fail(1, "required UI source missing");
    }

    if (!contains(xaml, "x:Name=\"TransferSurface\"") ||
        !contains(xaml, "x:Name=\"ProgressFill\"") ||
        !contains(xaml, "x:Name=\"ProgressPercentText\"") ||
        !contains(xaml, "x:Name=\"BrandLogo\"") ||
        !contains(xaml, "Source=\"Assets/VelocityCopy.png\"") ||
        !contains(xaml, "x:Name=\"PauseIcon\"") ||
        !contains(xaml, "x:Name=\"SkipIcon\"") ||
        !contains(xaml, "x:Name=\"StopIcon\"") ||
        !contains(xaml, "x:Name=\"CancelIcon\"") ||
        !contains(xaml, "x:Name=\"OptionsIcon\"") ||
        !contains(xaml, "x:Name=\"QueueButton\"") ||
        !contains(xaml, "x:Name=\"QueueChevron\"") ||
        !contains(xaml, "Glyph=\"&#xE70D;\"") ||
        !contains(xaml, "x:Name=\"OptionsButton\"")) {
        return fail(2, "collapsed copy bar must keep integrated progress, disclosure, and options");
    }

    if (contains(xaml, "x:Name=\"GlobalProgress\"") || contains(xaml, "<ProgressBar") || contains(xaml, "x:Name=\"ActionStrip\"")) {
        return fail(3, "collapsed mode must use the copier surface itself for progress");
    }

    if (!contains(header, "double progress_fraction_") ||
        !contains(window, "progress_fraction_ = (std::clamp)(fraction, 0.0, 1.0)") ||
        !contains(window, "ProgressFill().Width(TransferSurface().ActualWidth() * progress_fraction_)") ||
        !contains(window, "ProgressFill().Width(args.NewSize().Width * progress_fraction_)") ||
        !contains(execution, "SetProgressFraction(fraction)") ||
        !contains(queue, "QueueChevron().Glyph(expanding ? L\"\\xE70E\" : L\"\\xE70D\")")) {
        return fail(4, "runtime state must own progress logically and drive fill/disclosure rendering");
    }

    if (!contains(tokens, "<x:Double x:Key=\"WindowCollapsedHeight\">72</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"WindowExpandedHeight\">300</x:Double>")) {
        return fail(5, "compact and expanded geometry must remain aligned with the copy bar concept");
    }

    const auto logo_pos = xaml.find("x:Name=\"BrandLogo\"");
    const auto current_item_pos = xaml.find("x:Name=\"CurrentItemText\"");
    const auto queue_pos = xaml.find("x:Name=\"QueueButton\"");
    if (logo_pos == std::string::npos || current_item_pos == std::string::npos || queue_pos == std::string::npos ||
        !(logo_pos < current_item_pos && current_item_pos < queue_pos)) {
        return fail(6, "compact copy bar must keep logo left, content center, and queue disclosure right");
    }

    if (!contains(spec, "Integrated progress surface") ||
        !contains(spec, "brand mark at the far left") ||
        !contains(spec, "queue disclosure triangle at the far right") ||
        !contains(spec, "Progress fills left-to-right behind the content")) {
        return fail(7, "UI specification must document the integrated copy bar concept");
    }

    if (contains(xaml, "ShellFlowFlyout") || contains(xaml, "ShellFlowContent") || contains(xaml, "DestinationStep") ||
        contains(xaml, "LayoutStep") || contains(xaml, "StartCopyButton") || contains(xaml, "PreserveToggle") ||
        contains(xaml, "DirectToggle") || contains(window, "LoadDestinations") || contains(window, "SelectDestination") ||
        contains(window, "choose_layout") || contains(copy_append, "ShellFlowFlyout") || contains(shell, "BeginShellLayoutAsync")) {
        return fail(8, "drag/drop and shell transfers must not expose destination/layout menus in the copier window");
    }

    if (!contains(xaml, "x:Name=\"TitleBarDragRegion\"") || !contains(xaml, "Height=\"32\"") ||
        !contains(window, "SetTitleBar(TitleBarDragRegion())") || contains(xaml, "Canvas.ZIndex=\"-1\"") ||
        !contains(xaml, "x:Name=\"RootGrid\"") || !contains(xaml, "AllowDrop=\"True\"") ||
        !contains(xaml, "DragEnter=\"OnDragEnter\"") || !contains(xaml, "DragOver=\"OnDragOver\"") ||
        !contains(xaml, "DragLeave=\"OnDragLeave\"") || !contains(xaml, "Drop=\"OnDrop\"") ||
        contains(xaml, "x:Name=\"DragOverlay\"") || !contains(window, "accepts_active_transfer_drop") ||
        !contains(window, "DataPackageOperation::None")) {
        return fail(9, "window movement and whole-window append-only drop must remain usable");
    }

    if (!contains(xaml, "x:Name=\"QueuePanel\"") || !contains(xaml, "x:Name=\"QueueHeader\" MinHeight=\"28\"") ||
        !contains(window, "QueuePanel().Measure") || !contains(window, "QueuePanel().DesiredSize().Height") ||
        !contains(window, "std::numeric_limits<float>::infinity()") || !contains(queue, "ResizeWindowToContent()") ||
        !contains(xaml, "<RowDefinition Height=\"Auto\" />\n            <RowDefinition Height=\"Auto\" />") ||
        contains(xaml, "x:Name=\"QueuePanel\" CornerRadius=") || contains(xaml, "x:Name=\"QueuePanel\" Background=") ||
        contains(execution, "QueueButton().Content") || contains(queue, "QueueButton().Content") ||
        !contains(xaml, "FontFamily=\"Segoe Fluent Icons\"") ||
        !contains(xaml, "Background=\"{ThemeResource AccentFillColorDefaultBrush}\"") ||
        contains(xaml, "x:Name=\"TransferSurface\" CornerRadius=") || contains(xaml, "Background=\"#") ||
        contains(xaml, "BorderBrush=\"#") || contains(xaml, "Foreground=\"#")) {
        return fail(10, "queue disclosure must expand the same HWND from independently measured content and stay system-theme driven");
    }

    if (!contains(window, "#include \"MainWindow.g.cpp\"") ||
        !contains(window, "void MainWindow::ShowError") ||
        !contains(window, "hstring MainWindow::FormatFailureReason") ||
        !contains(window, "hstring MainWindow::FormatSpeed") ||
        !contains(window, "hstring MainWindow::FormatEta")) {
        return fail(11, "WinUI factory and shared window helpers must remain linked after structural cleanup");
    }

    if (!contains(xaml, "Click=\"OnQueueClick\"\n                            IsEnabled=\"True\"") ||
        !contains(execution, "void MainWindow::SetExecutionButtonsIdle()") ||
        !contains(execution, "QueueButton().IsEnabled(true)") ||
        !contains(window, "ProgressPercentText().Text(L\"<0.1%\")")) {
        return fail(12, "queue disclosure must remain available while idle and real sub-percent progress must not render as a frozen 0%");
    }

    return 0;
}
