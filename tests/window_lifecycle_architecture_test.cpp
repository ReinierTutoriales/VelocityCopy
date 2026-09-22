#include "architecture_support.hpp"
#include <filesystem>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read_source(ui/"MainWindow.Tray.cpp"), exec=read_source(ui/"MainWindow.Execution.cpp"), app=read_source(ui/"App.xaml.cpp");
 if(tray.find("self->HasActiveTransfer()") == std::string::npos || tray.find("self->HideToTray();") == std::string::npos) return 1;
 if(tray.find("self->tray_exit_requested_ = true;")==std::string::npos) return 2;
 if(exec.find("DestroyCompletedWindow();")==std::string::npos) return 3;
 if(app.find("auto main_window = winrt::make<MainWindow>();")==std::string::npos || app.find("windows_.insert_or_assign") == std::string::npos) return 4;
 const auto destroyed=body_of(app,"void App::OnWindowDestroyed");
 if(destroyed.empty() || destroyed.find("retiring_windows_") == std::string::npos || destroyed.find("windows_.erase(it)") == std::string::npos || destroyed.find("TryEnqueue") == std::string::npos) return 5;
 const auto enqueue=destroyed.find("TryEnqueue"), detach=destroyed.find("windows_.erase(it)");
 if(detach > enqueue) return 10;
 if(destroyed.find("ShowPrimaryWindow") != std::string::npos) return 11;
 const auto finish=body_of(exec,"void MainWindow::FinishCopy");
 if(finish.empty()) return 12;
 const auto pending=finish.find("HasPendingRecovery()");
 const auto destroy=finish.find("DestroyCompletedWindow();");
 if(pending == std::string::npos || destroy == std::string::npos || pending > destroy) return 13;
 if(app.find("OnExplicitShutdown")==std::string::npos) return 6;
 if(app.find("velocitycopy::route_transfer(")==std::string::npos || app.find("CreateMainWindow()") == std::string::npos) return 14;
 if(app.find("IsVisibleForRouting()") == std::string::npos || app.find("!implementation->HasActiveTransfer()") == std::string::npos) return 15;
 const auto create=body_of(app,"VelocityCopyUI::MainWindow App::CreateMainWindow(");
 if(create.empty() || create.find("MonitorFromWindow(")!=std::string::npos || create.find("GetMonitorInfoW(")!=std::string::npos) return 16;
 const auto deliver=body_of(app,"void App::DeliverConvertedJob(");
 if(deliver.empty() || deliver.find("MonitorFromWindow(reference_hwnd, MONITOR_DEFAULTTONEAREST)") == std::string::npos ||
    deliver.find("GetMonitorInfoW(") == std::string::npos || deliver.find("IsIconic(reference_hwnd)") == std::string::npos ||
    deliver.find("x + width > monitor_info.rcWork.right") == std::string::npos ||
    deliver.find("y + height > monitor_info.rcWork.bottom") == std::string::npos ||
    deliver.find("MoveNativeWindow(x, y)") == std::string::npos) return 17;
 if(app.find("weak_ref<winrt::VelocityCopyUI::MainWindow>")!=std::string::npos) return 7;
 if(app.find("DeliverShellRequest(request)") == std::string::npos) return 8;
 for (const auto& entry : std::filesystem::directory_iterator(ui)) {
  if (!entry.is_regular_file()) continue;
  const auto name=entry.path().filename().string();
  if(name.rfind("MainWindow.",0)==0 && entry.path().extension()==".cpp" && read_source(entry.path()).find("DestroyWindow(hwnd_)")!=std::string::npos) return 9;
 }
 return 0;
}
