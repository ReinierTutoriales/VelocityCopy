#pragma once

#include "App.xaml.g.h"

#include "velocitycopy/ipc_transport.hpp"

#include <memory>
#include <thread>

namespace winrt::VelocityCopyUI::implementation {
struct App : AppT<App> {
    App();
    ~App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    Microsoft::UI::Xaml::Window window_{nullptr};
    std::unique_ptr<velocitycopy::SingleInstance> instance_;
    std::shared_ptr<velocitycopy::ShellIpcServer> server_;
    std::jthread ipc_thread_;
};
}
