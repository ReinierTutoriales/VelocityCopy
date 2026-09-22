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
    const auto header = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto execution = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto append = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");
    const auto app = read_source(root / "src/ui/VelocityCopy.UI/App.xaml.cpp");
    const auto window = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    if (header.empty() || execution.empty() || append.empty() || app.empty() || window.empty()) {
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

    const auto deliver = body_of(app, "void App::DeliverShellRequest(");
    const auto pending = body_of(app, "void App::StartNextPendingRequest(");
    if (deliver.empty() || pending.empty()) return fail(4, "shell dispatch entry points missing");
    if (!contains(append, "dispatcher.TryEnqueue") ||
        contains(deliver, "resume_background()") || contains(deliver, "TryEnqueue") ||
        contains(pending, "resume_background()")) {
        return fail(4, "append planner completions marshal through DispatcherQueue; shell dispatch stays synchronously on UI thread");
    }

    const auto apply = execution.find("void MainWindow::ApplySnapshot");
    const auto finish = execution.find("void MainWindow::FinishCopy", apply);
    if (apply == std::string::npos || finish == std::string::npos || finish <= apply) {
        return fail(5, "authoritative UI snapshot consumer missing");
    }
    const auto apply_body = execution.substr(apply, finish - apply);
    if (!contains(apply_body, "SetProgressFraction(fraction)") ||
        !contains(apply_body, "SpeedText().Text") ||
        !contains(apply_body, "EtaText().Text") ||
        !contains(window, "void MainWindow::SetProgressFraction") ||
        !contains(window, "ProgressFill().Width") ||
        !contains(window, "ProgressPercentText().Text")) {
        return fail(6, "progress rendering must remain centralized behind the UI-thread snapshot consumer");
    }

    return 0;
}
