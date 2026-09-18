#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "velocitycopy/process_activation.hpp"

#include <shellapi.h>

#include <cstdlib>
#include <optional>

namespace winrt::VelocityCopyUI::implementation {
namespace {

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

bool is_stage_only_activation(const std::optional<velocitycopy::ShellRequest>& request) noexcept {
    return request &&
        request->action == velocitycopy::ShellAction::CopySelection &&
        velocitycopy::shell_request_valid(*request);
}

bool is_startup_activation() noexcept {
    try {
        const auto args = Microsoft::Windows::AppLifecycle::AppInstance::GetCurrent().GetActivatedEventArgs();
        return args &&
            args.Kind() == Microsoft::Windows::AppLifecycle::ExtendedActivationKind::StartupTask;
    } catch (...) {
        return false;
    }
}

} // namespace

App::App() {
    InitializeComponent();
}

App::~App() {
    if (server_) {
        server_->stop();
    }
    ipc_thread_.request_stop();
    if (ipc_thread_.joinable()) {
        ipc_thread_.join();
    }
    server_.reset();
    instance_.reset();
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    const bool startup_activation = is_startup_activation();
    const auto initial_request = inherited_shell_request();

    instance_ = std::make_unique<velocitycopy::SingleInstance>();
    if (!instance_->valid()) {
        Microsoft::UI::Xaml::Application::Current().Exit();
        return;
    }

    if (!instance_->primary()) {
        if (initial_request) {
            (void)velocitycopy::send_shell_request(*initial_request, 1000);
        } else if (!startup_activation) {
            velocitycopy::ShellRequest open{};
            open.action = velocitycopy::ShellAction::OpenVelocityCopy;
            (void)velocitycopy::send_shell_request(open, 1000);
        }
        Microsoft::UI::Xaml::Application::Current().Exit();
        return;
    }

    auto main_window = winrt::make<MainWindow>();
    window_ = main_window;
    if (!startup_activation && !is_stage_only_activation(initial_request)) {
        if (auto* implementation = winrt::get_self<MainWindow>(main_window)) {
            implementation->ShowFromTray();
        }
    }

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

    server_ = std::make_shared<velocitycopy::ShellIpcServer>();
    if (!server_->valid()) {
        server_.reset();
        return;
    }

    const auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    auto server = server_;
    ipc_thread_ = std::jthread(
        [server, dispatcher, deliver = std::move(deliver)](std::stop_token stop_token) mutable {
            while (!stop_token.stop_requested() && !server->stopping()) {
                auto request = server->receive();
                if (!request) {
                    if (stop_token.stop_requested() || server->stopping()) {
                        return;
                    }
                    // A malformed/aborted client must not permanently stop Explorer integration.
                    continue;
                }

                auto value = std::move(*request);
                if (!dispatcher.TryEnqueue([deliver, value = std::move(value)]() mutable {
                        deliver(value);
                    })) {
                    server->stop();
                    return;
                }
            }
        });
}
}
