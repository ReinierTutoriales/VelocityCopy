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
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto tokens = read_all(root / "src/ui/DesignTokens.xaml");
    const auto spec = read_all(root / "docs/UI_SPEC.md");

    if (xaml.empty() || execution.empty() || queue.empty() || tokens.empty() || spec.empty()) {
        return fail(1, "required UI source missing");
    }

    if (!contains(xaml, "x:Name=\"TransferSurface\"") ||
        !contains(xaml, "x:Name=\"ProgressFill\"") ||
        !contains(xaml, "x:Name=\"ProgressPercentText\"") ||
        !contains(xaml, "x:Name=\"QueueButton\"") ||
        !contains(xaml, "Content=\"▸\"") ||
        !contains(xaml, "x:Name=\"OptionsButton\"")) {
        return fail(2, "collapsed copy bar must keep integrated progress, disclosure, and options");
    }

    if (!contains(xaml, "x:Name=\"GlobalProgress\"") ||
        !contains(xaml, "Visibility=\"Collapsed\"") ||
        contains(xaml, "x:Name=\"ActionStrip\"")) {
        return fail(3, "collapsed mode must not regress to a separate visible progress/action strip");
    }

    if (!contains(execution, "ProgressFill().Width(TransferSurface().ActualWidth() * fraction)") ||
        !contains(execution, "ProgressPercentText().Text") ||
        !contains(queue, "QueueButton().Content(box_value(hstring(expanding ? L\"▾\" : L\"▸\")))")) {
        return fail(4, "runtime state must drive the integrated fill and disclosure direction");
    }

    if (!contains(tokens, "<x:Double x:Key=\"WindowCollapsedHeight\">72</x:Double>") ||
        !contains(tokens, "<x:Double x:Key=\"WindowExpandedHeight\">300</x:Double>")) {
        return fail(5, "compact and expanded geometry must remain aligned with the copy bar concept");
    }

    if (!contains(spec, "Integrated progress surface") ||
        !contains(spec, "left disclosure triangle") ||
        !contains(spec, "Progress fills the compact transfer surface")) {
        return fail(6, "UI specification must document the integrated copy bar concept");
    }

    return 0;
}
