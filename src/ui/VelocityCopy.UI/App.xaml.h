#pragma once

#include "App.xaml.g.h"
#include "AppTray.h"

#include "velocitycopy/efficiency_coordinator.hpp"
#include "velocitycopy/ipc_transport.hpp"

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace winrt::VelocityCopyUI::implementation {
struct App : AppT<App> {
    App();
    ~App();
    static App* Instance() noexcept { return s_instance; }
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    void ReportEfficiencyVote(std::uint64_t window_id, bool eligible) noexcept;
    void RemoveEfficiencyVote(std::uint64_t window_id) noexcept;
    void SetShuttingDown(bool value) noexcept;
    std::uint64_t NextWindowId() noexcept;
    void ShowPrimaryWindow();
    void ExitFromTray() noexcept;
    void DeliverShellRequest(const velocitycopy::ShellRequest& request);
    void OnWindowDestroyed(std::uint64_t window_id) noexcept;
    std::optional<std::filesystem::path> TakeRecoveryFile() noexcept;
    void ReturnRecoveryFile(std::filesystem::path path) noexcept;
    [[nodiscard]] bool HasPendingRecovery() const noexcept { return !pending_recovery_files_.empty(); }

private:
    static inline App* s_instance = nullptr;
    void ApplyEfficiencyMode(bool enabled) noexcept;
    void InitializeRecoveryFiles() noexcept;
    Microsoft::UI::Xaml::Window window_{nullptr};
    std::vector<Microsoft::UI::Xaml::Window> retiring_windows_;
    std::deque<std::filesystem::path> pending_recovery_files_;
    bool recovery_files_initialized_{};
    AppTray tray_;
    std::unique_ptr<velocitycopy::SingleInstance> instance_;
    std::shared_ptr<velocitycopy::ShellIpcServer> server_;
    std::jthread ipc_thread_;
    velocitycopy::EfficiencyCoordinator efficiency_coordinator_;
    bool efficiency_mode_enabled_{};
    std::uint64_t next_window_id_{1};
};
}
