#pragma once

#include "App.xaml.g.h"
#include "AppTray.h"

#include "velocitycopy/efficiency_coordinator.hpp"
#include "velocitycopy/ipc_transport.hpp"

#include <memory>
#include <thread>

namespace winrt::VelocityCopyUI::implementation {
struct App : AppT<App> {
    App();
    ~App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    void ReportEfficiencyVote(std::uint64_t window_id, bool eligible) noexcept;
    void RemoveEfficiencyVote(std::uint64_t window_id) noexcept;
    void SetShuttingDown(bool value) noexcept;
    std::uint64_t NextWindowId() noexcept;
    void ShowPrimaryWindow();
    void ExitFromTray() noexcept;
    void OnWindowDestroyed(std::uint64_t window_id) noexcept;

private:
    void ApplyEfficiencyMode(bool enabled) noexcept;
    Microsoft::UI::Xaml::Window window_{nullptr};
    AppTray tray_;
    std::unique_ptr<velocitycopy::SingleInstance> instance_;
    std::shared_ptr<velocitycopy::ShellIpcServer> server_;
    std::jthread ipc_thread_;
    velocitycopy::EfficiencyCoordinator efficiency_coordinator_;
    bool efficiency_mode_enabled_{};
    std::uint64_t next_window_id_{1};
};
}
