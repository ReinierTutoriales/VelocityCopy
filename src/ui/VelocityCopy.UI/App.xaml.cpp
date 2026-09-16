#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/process_activation.hpp"

#include <shellapi.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>

namespace winrt::VelocityCopyUI::implementation {
namespace {

std::unique_ptr<velocitycopy::SingleInstance> g_instance;
std::shared_ptr<velocitycopy::ShellIpcServer> g_server;

std::optional<std::uintptr_t> parse_uintptr(const wchar_t* text) noexcept {
    if (text == nullptr || *text == L'\0') {
        return std::nullopt;
    }

    wchar_t* end = nullptr;
    const auto value = std::wcstoull(text, &end, 10);
    if (end == text || *end != L'\0') {
        return std::nullopt;
    }
    return static_cast<std::uintptr_t>(value);
}

std::optional<std::size_t> parse_size(const wchar_t* text) noexcept {
    const auto value = parse_uintptr(text);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

std::optional<velocitycopy::ShellRequest> inherited_shell_request() noexcept {
    int argc = 0;
    auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return std::nullopt;
    }

    std::optional<velocitycopy::ShellRequest> request;
    if (argc == 6 && std::wstring_view(argv[1]) == L"--shell-runtime" &&
        std::wstring_view(argv[2]) == L"--activation-handle" &&
        std::wstring_view(argv[4]) == L"--activation-size") {
        const auto handle = parse_uintptr(argv[3]);
        const auto size = parse_size(argv[5]);
        if (handle && size) {
            request = velocitycopy::read_inherited_shell_request(*handle, *size);
        }
    }

    LocalFree(argv);
    return request;
}

} // namespace

App::App() {
    InitializeComponent();
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    const auto initial_request = inherited_shell_request();

    g_instance = std::make_unique<velocitycopy::SingleInstance>();
    if (!g_instance->valid()) {
        Microsoft::UI::Xaml::Application::Current().Exit();
        return;
    }

    if (!g_instance->primary()) {
        if (initial_request) {
            (void)velocitycopy::send_shell_request(*initial_request, 1000);
        }
        Microsoft::UI::Xaml::Application::Current().Exit();
        return;
    }

    auto main_window = winrt::make<MainWindow>();
    window_ = main_window;
    window_.Activate();

    auto deliver = [weak = winrt::weak_ref<winrt::VelocityCopyUI::MainWindow>{main_window}](
                       const velocitycopy::ShellRequest& request) {
        if (auto projected = weak.get()) {
            if (auto* implementation = winrt::get_self<MainWindow>(projected)) {
                implementation->HandleShellRequest(request);
            }
        }
    };

    if (initial_request) {
        deliver(*initial_request);
    }

    g_server = std::make_shared<velocitycopy::ShellIpcServer>();
    if (!g_server->valid()) {
        return;
    }

    const auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    auto server = g_server;
    std::thread([server, dispatcher, deliver = std::move(deliver)]() mutable {
        for (;;) {
            auto request = server->receive();
            if (!request) {
                return;
            }

            auto value = std::move(*request);
            if (!dispatcher.TryEnqueue([deliver, value = std::move(value)]() mutable {
                    deliver(value);
                })) {
                return;
            }
        }
    }).detach();
}
}
