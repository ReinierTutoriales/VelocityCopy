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
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
std::size_t count_occurrences(const std::string& text, const std::string& value) {
    std::size_t count = 0;
    for (std::size_t offset = 0; (offset = text.find(value, offset)) != std::string::npos; offset += value.size()) ++count;
    return count;
}
int fail(int code, const char* message) {
    std::cerr << "UI threading architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto header = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto append = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");
    const auto shell = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    if (header.empty() || execution.empty() || append.empty() || shell.empty() || window.empty()) {
        return fail(1, "required WinUI source missing");
    }

    if (!contains(header, "Microsoft::UI::Dispatching::DispatcherQueue dispatcher_") ||
        !contains(window, "DispatcherQueue::GetForCurrentThread()")) {
        return fail(2, "UI-thread DispatcherQueue ownership missing");
    }

    if (!contains(execution, "dispatcher.TryEnqueue([weak, value]()") ||
        !contains(execution, "self->ApplySnapshot(value)") ||
        !contains(execution, "dispatcher.TryEnqueue([weak, result]()") ||
        count_occurrences(execution, "copy_thread_ = std::jthread") < 2) {
        return fail(3, "copy workers must marshal progress and completion through DispatcherQueue");
    }

    if (!contains(append, "dispatcher.TryEnqueue") ||
        !contains(shell, "dispatcher.TryEnqueue")) {
        return fail(4, "append and shell background completions must marshal through DispatcherQueue");
    }

    const auto apply = execution.find("void MainWindow::ApplySnapshot");
    const auto finish = execution.find("void MainWindow::FinishCopy", apply);
    if (apply == std::string::npos || finish == std::string::npos ||
        finish <= apply) {
        return fail(5, "authoritative UI snapshot consumer missing");
    }
    const auto apply_body = execution.substr(apply, finish - apply);
    if (!contains(apply_body, "GlobalProgress().Value") ||
        !contains(apply_body, "ProgressFill().Width") ||
        !contains(apply_body, "ProgressPercentText().Text") ||
        !contains(apply_body, "SpeedText().Text") ||
        !contains(apply_body, "EtaText().Text")) {
        return fail(6, "progress controls must remain centralized in ApplySnapshot");
    }

    return 0;
}
