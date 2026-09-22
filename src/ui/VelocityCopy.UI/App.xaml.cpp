#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/app_storage.hpp"
#include "velocitycopy/diagnostics.hpp"
#include "velocitycopy/transfer_router.hpp"

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
            std::vector<std::wstring> active_session_ids;
            active_session_ids.reserve(windows_.size());
            for (auto& [id, window] : windows_) {
                (void)id;
                if (auto main_window = window.try_as<VelocityCopyUI::MainWindow>()) {
                    if (auto* implementation = get_self<MainWindow>(main_window); implementation && !implementation->SessionId().empty())
                        active_session_ids.push_back(implementation->SessionId());
                }
            }
            const auto files = velocitycopy::list_recovery_files(*dir, active_session_ids);
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

VelocityCopyUI::MainWindow App::CreateMainWindow() {
    auto main_window = winrt::make<MainWindow>();
    if (auto* implementation = get_self<MainWindow>(main_window)) {
        windows_.insert_or_assign(implementation->WindowId(), main_window);
    }
    return main_window;
}

void App::DeliverShellRequest(const velocitycopy::ShellRequest& request) {
    velocitycopy::ShellDispatchResult dispatch{};
    try { dispatch = shell_session_.dispatch(request); }
    catch (...) { velocitycopy::log_diagnostic(L"shell: transfer request failed during dispatch"); ShowPrimaryWindowError(); return; }
    if (dispatch.status != velocitycopy::ShellDispatchStatus::Accepted) {
        ShowPrimaryWindowError();
        return;
    }
    if (dispatch.job) {
        pending_requests_.push_back(std::move(*dispatch.job));
        StartNextPendingRequest();
        return;
    }
    if (dispatch.show_window) ShowPrimaryWindow();
}

void App::StartNextPendingRequest() {
    if (request_in_flight_ || pending_requests_.empty()) return;
    request_in_flight_ = true;
    auto job = std::move(pending_requests_.front());
    pending_requests_.pop_front();
    ResolveStorageKeysAsync(std::move(job));
}

winrt::fire_and_forget App::ResolveStorageKeysAsync(velocitycopy::CopyJob job) {
    auto lifetime = get_strong();
    auto ui_thread = winrt::apartment_context{};
    velocitycopy::StorageKey destination_key{};
    velocitycopy::StorageKey source_key{};
    bool failed = false;
    try {
        co_await winrt::resume_background();
        destination_key = velocitycopy::resolve_storage_key(job.destination);
        std::vector<std::filesystem::path> parents;
        parents.reserve(job.sources.size());
        for (const auto& source : job.sources) {
            auto parent = source.parent_path();
            if (std::find(parents.begin(), parents.end(), parent) == parents.end()) parents.push_back(std::move(parent));
        }
        if (!parents.empty()) {
            source_key = velocitycopy::resolve_storage_key(parents.front());
            for (std::size_t index = 1; index < parents.size(); ++index) {
                const auto candidate = velocitycopy::resolve_storage_key(parents[index]);
                if (!velocitycopy::same_device(source_key, candidate)) {
                    source_key = {};
                    break;
                }
            }
        }
    } catch (...) {
        failed = true;
        velocitycopy::log_diagnostic(L"shell: storage-key resolution failed");
    }

    try {
        co_await ui_thread;
    } catch (...) {
        co_return;
    }
    try {
        if (failed) ShowPrimaryWindowError();
        else DeliverConvertedJob(std::move(job), std::move(destination_key), std::move(source_key));
    } catch (...) {
        velocitycopy::log_diagnostic(L"shell: transfer request failed after storage-key resolution");
        ShowPrimaryWindowError();
    }
    request_in_flight_ = false;
    StartNextPendingRequest();
}

void App::DeliverConvertedJob(velocitycopy::CopyJob job, velocitycopy::StorageKey destination_key, velocitycopy::StorageKey source_key) {
    const velocitycopy::TransferRequest request{job.destination, job.operation, destination_key, source_key};
    auto snapshot = [this] {
        std::vector<velocitycopy::ActiveSession> sessions;
        sessions.reserve(windows_.size());
        for (auto& [id, window] : windows_) {
            (void)id;
            if (auto main_window = window.try_as<VelocityCopyUI::MainWindow>()) {
                if (auto* implementation = get_self<MainWindow>(main_window)) {
                    if (auto session = implementation->SessionSnapshot()) sessions.push_back(std::move(*session));
                }
            }
        }
        return sessions;
    };
    auto find_window = [this](const std::uint64_t id) -> MainWindow* {
        const auto it = windows_.find(id);
        if (it == windows_.end()) return nullptr;
        if (auto main_window = it->second.try_as<VelocityCopyUI::MainWindow>()) return get_self<MainWindow>(main_window);
        return nullptr;
    };

    velocitycopy::RouteResult route{};
    for (int attempt = 0; attempt != 2; ++attempt) {
        const auto sessions = snapshot();
        route = velocitycopy::route_transfer(request, sessions, route_preferences_);
        if (route.decision == velocitycopy::RouteDecision::Ask) {
            auto* dialog_owner = find_window(route.window_id);
            if (dialog_owner == nullptr || route.offered.size() != 2) {
                velocitycopy::log_diagnostic(L"shell: routing decision dialog has no valid owner/options");
                return;
            }

            const bool same_destination_prompt =
                route.offered[0] == velocitycopy::RouteChoice::Append &&
                route.offered[1] == velocitycopy::RouteChoice::Wait;
            const std::wstring title = same_destination_prompt
                ? L"Destination already in use"
                : L"Storage device already in use";
            const std::wstring message = same_destination_prompt
                ? L"A transfer to this destination is already running. Add these files to it or wait?"
                : L"Another transfer is using the same storage device. Wait or run this transfer in parallel?";
            const std::wstring primary = same_destination_prompt ? L"Add" : L"Wait";
            const std::wstring secondary = same_destination_prompt ? L"Wait" : L"Parallel";

            bool remember = false;
            const auto choice = MainWindow::ShowNativeDecisionDialog(
                dialog_owner->NativeOwner(), title, message, primary, secondary, false, {}, &remember);
            if (choice == MainWindow::NativeDialogChoice::Cancel) {
                velocitycopy::log_diagnostic(L"shell: routing decision cancelled");
                return;
            }

            const auto selected = choice == MainWindow::NativeDialogChoice::Primary
                ? route.offered[0]
                : route.offered[1];
            if (remember) {
                if (same_destination_prompt) route_preferences_.same_destination = selected;
                else route_preferences_.same_device = selected;
            }
            route.decision = selected == velocitycopy::RouteChoice::Append
                ? velocitycopy::RouteDecision::AppendTo
                : selected == velocitycopy::RouteChoice::Wait
                    ? velocitycopy::RouteDecision::WaitFor
                    : velocitycopy::RouteDecision::StartNew;
        }

        if (route.decision == velocitycopy::RouteDecision::AppendTo) {
            auto* target = find_window(route.window_id);
            const auto current = target ? target->SessionSnapshot() : std::nullopt;
            if (!target || !current || !current->accepting_appends) {
                if (attempt == 0) continue;
                route.decision = velocitycopy::RouteDecision::StartNew;
            } else {
                target->ShowFromTray();
                target->AppendTransfer(std::move(job));
                return;
            }
        }
        if (route.decision == velocitycopy::RouteDecision::WaitFor) {
            if (auto* target = find_window(route.window_id)) {
                target->ShowFromTray();
                target->EnqueueTransfer(std::move(job), std::move(destination_key), std::move(source_key));
                return;
            }
            if (attempt == 0) continue;
            route.decision = velocitycopy::RouteDecision::StartNew;
        }
        if (route.decision == velocitycopy::RouteDecision::StartNew) break;
    }

    MainWindow* target = nullptr;
    for (auto& [id, window] : windows_) {
        (void)id;
        if (auto main_window = window.try_as<VelocityCopyUI::MainWindow>()) {
            if (auto* implementation = get_self<MainWindow>(main_window);
                implementation && !implementation->HasActiveTransfer() && implementation->IsVisibleForRouting()) {
                target = implementation;
                break;
            }
        }
    }
    if (!target) {
        HWND reference_hwnd = nullptr;
        if (!windows_.empty()) {
            if (auto reference_window = windows_.rbegin()->second.try_as<VelocityCopyUI::MainWindow>()) {
                if (auto* reference = get_self<MainWindow>(reference_window)) reference_hwnd = reference->NativeOwner();
            }
        }

        auto window = CreateMainWindow();
        target = get_self<MainWindow>(window);
        if (target && reference_hwnd != nullptr) {
            MONITORINFO monitor_info{sizeof(monitor_info)};
            const HMONITOR monitor = MonitorFromWindow(reference_hwnd, MONITOR_DEFAULTTONEAREST);
            RECT reference_rect{};
            RECT new_rect{};
            if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) &&
                GetWindowRect(target->NativeOwner(), &new_rect)) {
                const int width = new_rect.right - new_rect.left;
                const int height = new_rect.bottom - new_rect.top;
                const UINT dpi = GetDpiForWindow(reference_hwnd);
                const int offset = MulDiv(32, dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
                const int margin = offset;
                int x = monitor_info.rcWork.left + margin;
                int y = monitor_info.rcWork.top + margin;

                if (!IsIconic(reference_hwnd) && GetWindowRect(reference_hwnd, &reference_rect)) {
                    x = reference_rect.left + offset;
                    y = reference_rect.top + offset;
                    if (x + width > monitor_info.rcWork.right || y + height > monitor_info.rcWork.bottom) {
                        x = monitor_info.rcWork.left + margin;
                        y = monitor_info.rcWork.top + margin;
                    }
                }
                target->MoveNativeWindow(x, y);
            }
        }
    }
    if (target) {
        target->ShowFromTray();
        target->StartTransfer(std::move(job), std::move(destination_key), std::move(source_key));
    }
}

void App::ShowPrimaryWindowError() noexcept {
    try {
        Microsoft::UI::Xaml::Window target{nullptr};
        if (!windows_.empty()) target = windows_.rbegin()->second;
        if (!target) target = CreateMainWindow();
        if (auto main_window = target.try_as<VelocityCopyUI::MainWindow>()) if (auto* implementation = get_self<MainWindow>(main_window)) implementation->ShowRequestError();
    } catch (...) {
        velocitycopy::log_diagnostic(L"shell: could not show request error window");
    }
}

void App::ShowPrimaryWindow() {
    Microsoft::UI::Xaml::Window target{nullptr};
    if (!windows_.empty()) target = windows_.rbegin()->second;
    if (!target) target = CreateMainWindow();
    if (auto main_window = target.try_as<VelocityCopyUI::MainWindow>()) {
        if (auto* implementation = get_self<MainWindow>(main_window)) {
            implementation->ShowFromTray();
            implementation->OfferRecoveryIfIdle();
        }
    }
}

void App::OnWindowDestroyed(const std::uint64_t window_id) noexcept {
    RemoveEfficiencyVote(window_id);
    try {
        // Detach routing immediately: IPC/tray actions must never reuse a Window
        // after its HWND has entered WM_DESTROY. Keep a strong reference separately
        // until the native subclass callback has fully unwound.
        if (const auto it = windows_.find(window_id); it != windows_.end()) {
            retiring_windows_.push_back(it->second);
            windows_.erase(it);
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
    for (auto& [id, window] : windows_) {
        (void)id;
        if (auto main_window = window.try_as<VelocityCopyUI::MainWindow>()) {
            if (auto* implementation = get_self<MainWindow>(main_window)) implementation->RequestAppExit();
        }
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

    auto main_window = CreateMainWindow();
    if (!tray_.Initialize(this)) {
        const auto error = GetLastError();
        velocitycopy::log_diagnostic(L"tray: initialization failed (Win32 " + std::to_wstring(error) + L")");
    }
    if (!startup_activation) {
        if (auto* implementation = winrt::get_self<MainWindow>(main_window)) {
            implementation->ShowFromTray();
            if (!initial_request) {
                implementation->OfferRecoveryIfIdle();
            }
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