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

    if (!contains(execution, "dispatcher.TryEnqueue([weak, control, mailbox]()") ||
        !contains(execution, "self->ApplySnapshot(*value)") ||
        !contains(execution, "self->execution_control_ == control") ||
        !contains(execution, "mailbox->consume()") ||
        !contains(execution, "mailbox->discard()") ||
        !contains(execution, "dispatcher.TryEnqueue([weak, result]()") ||
        count_occurrences(execution, "copy_thread_ = std::jthread") < 2) {
        return fail(3, "copy workers must marshal progress and completion through DispatcherQueue");
    }

    const auto deliver = body_of(app, "void App::DeliverShellRequest(");
    const auto pending = body_of(app, "void App::StartNextPendingRequest(");
    if (deliver.empty() || pending.empty()) return fail(4, "shell dispatch entry points missing");
    if (!contains(append, "dispatcher.TryEnqueue") ||
        contains(deliver, "resume_background()") || contains(deliver, "TryEnqueue") ||
        contains(pending, "resume_background()") || contains(pending, "TryEnqueue")) {
        return fail(4, "append planner completions marshal through DispatcherQueue; shell dispatch stays synchronously on UI thread");
    }


    const auto resolve = body_of(app, "App::ResolveStorageKeysAsync(");
    if (resolve.empty() || !contains(resolve, "resume_background()")) return fail(5, "storage-key resolution must run off the UI thread");
    if (resolve.find("resolve_storage_key(") < resolve.find("resume_background()")) return fail(5, "resolve_storage_key must run after resume_background");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "src/ui")) {
        if (!entry.is_regular_file()) continue;
        const auto text = read_source(entry.path());
        const auto total = count_occurrences(text, "resolve_storage_key(");
        const auto outside = entry.path().filename() == "App.xaml.cpp"
            ? total - count_occurrences(resolve, "resolve_storage_key(")
            : total;
        if (outside != 0) return fail(5, "resolve_storage_key must only be called from ResolveStorageKeysAsync");
    }

    const auto apply = execution.find("void MainWindow::ApplySnapshot");
    const auto finish = execution.find("void MainWindow::FinishCopy", apply);
    if (apply == std::string::npos || finish == std::string::npos || finish <= apply) {
        return fail(6, "authoritative UI snapshot consumer missing");
    }
    const auto apply_body = execution.substr(apply, finish - apply);
    if (!contains(apply_body, "SetProgressFraction(fraction)") ||
        !contains(apply_body, "SpeedText().Text") ||
        !contains(apply_body, "EtaText().Text") ||
        !contains(window, "void MainWindow::SetProgressFraction") ||
        !contains(window, "TransferProgress().Value(percent)") ||
        !contains(window, "ProgressPercentText().Text")) {
        return fail(7, "progress rendering must remain centralized behind the UI-thread snapshot consumer");
    }

    const auto scale_callback = body_of(window,
        "[weak, dispatcher = self->dispatcher_](Windows::UI::ViewManagement::UISettings const& sender");
    const auto enqueue = scale_callback.find("dispatcher.TryEnqueue");
    if (enqueue == std::string::npos ||
        contains(scale_callback.substr(0, enqueue), "weak.get()") ||
        contains(scale_callback.substr(0, enqueue), "last_text_scale_factor_") ||
        !contains(scale_callback.substr(enqueue), "last_text_scale_factor_ = scale")) {
        return fail(8, "text-scale callbacks must marshal before accessing window state");
    }
    if (!contains(apply_body, "if (paused_ ||") || apply_body.find("if (paused_ ||") > apply_body.find("SetProgressFraction")) {
        return fail(9, "queued progress must not overwrite paused telemetry");
    }

    return 0;
}
