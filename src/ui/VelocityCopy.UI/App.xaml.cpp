#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/app_storage.hpp"

#include <shellapi.h>

#include <chrono>
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

// Classic deployment receives startup intent explicitly via --startup.
bool is_startup_activation() noexcept {
    // Classic/unpackaged startup is explicit. The installer is the only component
    // allowed to register the HKCU Run entry; runtime code must never repair it.
    int argc = 0;
    auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }
    bool startup = false;
    for (int index = 1; index < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--startup") {
            startup = true;
            break;
        }
    }
    LocalFree(argv);
    return startup;
}

bool deliver_to_primary(const velocitycopy::ShellRequest& request) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        if (velocitycopy::send_shell_request(request, 250)) {
            return true;
        }
        Sleep(100);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void show_activation_error(const wchar_t* message) noexcept {
    MessageBoxW(nullptr, message, L"VelocityCopy",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

} // namespace

App::App() {
    s_instance = this;
    InitializeComponent();
    DispatcherShutdownMode(Microsoft::UI::Xaml::DispatcherShutdownMode::OnExplicitShutdown);
}

App::~App() {
    tray_.Remove();
    if (server_) {
        server_->stop();
    }
    ipc_thread_.request_stop();
    if (ipc_thread_.joinable()) {
        ipc_thread_.join();
    }
    server_.reset();
    instance_.reset();
    s_instance = nullptr;
}


void App::InitializeRecoveryFiles() noexcept {
    if (recovery_files_initialized_) return;
    recovery_files_initialized_ = true;
    try {
        if (const auto dir = velocitycopy::app_data_directory()) {
            const auto files = velocitycopy::list_recovery_files(*dir);
            pending_recovery_files_.assign(files.begin(), files.end());
        }
    } catch (...) {}
}

std::optional<std::filesystem::path> App::TakeRecoveryFile() noexcept {
    InitializeRecoveryFiles();
    if (pending_recovery_files_.empty()) return std::nullopt;
    auto path = std::move(pending_recovery_files_.front());
    pending_recovery_files_.pop_front();
    return path;
}

void App::ReturnRecoveryFile(std::filesystem::path path) noexcept {
    try { pending_recovery_files_.push_front(std::move(path)); } catch (...) {}
}

void App::ApplyEfficiencyMode(const bool enabled) noexcept {
    if (efficiency_mode_enabled_ == enabled) return;
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = enabled ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) {
        efficiency_mode_enabled_ = enabled;
    }
}

void App::ReportEfficiencyVote(const std::uint64_t window_id, const bool eligible) noexcept {
    try { ApplyEfficiencyMode(efficiency_coordinator_.update(window_id, eligible)); } catch (...) {}
}

void App::RemoveEfficiencyVote(const std::uint64_t window_id) noexcept {
    try { ApplyEfficiencyMode(efficiency_coordinator_.remove(window_id)); } catch (...) {}
}

void App::SetShuttingDown(const bool value) noexcept {
    try { ApplyEfficiencyMode(efficiency_coordinator_.set_shutting_down(value)); } catch (...) {}
}

std::uint64_t App::NextWindowId() noexcept {
    return next_window_id_++;
}

void App::DeliverShellRequest(const velocitycopy::ShellRequest& request) {
    auto main_window = window_.try_as<VelocityCopyUI::MainWindow>();
    if (!main_window) {
        main_window = winrt::make<MainWindow>();
        window_ = main_window;
    }
    if (auto* implementation = get_self<MainWindow>(main_window)) implementation->HandleShellRequest(request);
}

void App::ShowPrimaryWindow() {
    auto main_window = window_.try_as<VelocityCopyUI::MainWindow>();
    if (!main_window) {
        main_window = winrt::make<MainWindow>();
        window_ = main_window;
    }
    if (auto* implementation = get_self<MainWindow>(main_window)) implementation->ShowFromTray();
}

void App::OnWindowDestroyed(const std::uint64_t window_id) noexcept {
    RemoveEfficiencyVote(window_id);
    try {
        // Detach routing immediately: IPC/tray actions must never reuse a Window
        // after its HWND has entered WM_DESTROY. Keep a strong reference separately
        // until the native subclass callback has fully unwound.
        if (auto current = window_.try_as<VelocityCopyUI::MainWindow>()) {
            if (auto* implementation = get_self<MainWindow>(current);
                implementation && implementation->WindowId() == window_id) {
                retiring_windows_.push_back(window_);
                window_ = nullptr;
            }
        }
        auto weak = get_weak();
        (void)Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
            [weak] {
                if (auto self = weak.get()) self->retiring_windows_.clear();
            });
    } catch (...) {}
}

void App::ExitFromTray() noexcept {
    SetShuttingDown(true);
    tray_.Remove();
    if (auto main_window = window_.try_as<VelocityCopyUI::MainWindow>()) {
        if (auto* implementation = get_self<MainWindow>(main_window)) implementation->RequestAppExit();
    }
    Microsoft::UI::Xaml::Application::Current().Exit();
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    const bool startup_activation = is_startup_activation();
    const auto initial_request = inherited_shell_request();

    instance_ = std::make_unique<velocitycopy::SingleInstance>();
    if (!instance_->valid()) {
        if (!startup_activation) {
            show_activation_error(
                L"VelocityCopy is already running with different permissions. "
                L"Close it from the notification area and try again.");
        }
        Microsoft::UI::Xaml::Application::Current().Exit();
        return;
    }

    if (!instance_->primary()) {
        bool delivered = true;
        if (initial_request) {
            delivered = deliver_to_primary(*initial_request);
        } else if (!startup_activation) {
            velocitycopy::ShellRequest open{};
            open.action = velocitycopy::ShellAction::OpenVelocityCopy;
            delivered = deliver_to_primary(open);
        }
        if (!delivered && !startup_activation) {
            show_activation_error(
                initial_request
                    ? L"VelocityCopy is running but did not respond. The transfer was not started."
                    : L"VelocityCopy is running but did not respond.");
        }
        Microsoft::UI::Xaml::Application::Current().Exit();
        return;
    }

    auto main_window = winrt::make<MainWindow>();
    window_ = main_window;
    if (!tray_.Initialize(this)) {
        OutputDebugStringW(L"VelocityCopy: failed to initialize notification-area integration.\n");
    }
    if (!startup_activation) {
        if (auto* implementation = winrt::get_self<MainWindow>(main_window)) {
            implementation->ShowFromTray();
        }
    }

    auto deliver = [weak = get_weak()](const velocitycopy::ShellRequest& request) {
        if (auto self = weak.get()) self->DeliverShellRequest(request);
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
