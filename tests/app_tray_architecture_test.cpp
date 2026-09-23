#include <filesystem>
#include <fstream>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read(ui/"AppTray.cpp"), tray_h=read(ui/"AppTray.h"), main=read(ui/"MainWindow.Tray.cpp"), app=read(ui/"App.xaml.h"), app_cpp=read(ui/"App.xaml.cpp"), window_cpp=read(ui/"MainWindow.xaml.cpp"), conflict=read(ui/"MainWindow.Conflict.cpp");
 if(tray.find("Shell_NotifyIconW")==std::string::npos || tray.find("WS_EX_TOOLWINDOW")==std::string::npos) return 1;
 if(tray.find("HWND_MESSAGE")!=std::string::npos) return 4;
 if(tray.find("PostMessageW(hwnd_, WM_NULL") == std::string::npos) return 5;
 if(main.find("Shell_NotifyIconW")!=std::string::npos || main.find("NOTIFYICONDATA")!=std::string::npos) return 2;
 if(app.find("AppTray tray_")==std::string::npos) return 3;
 if(tray.find("DefWindowProcW(hwnd_")!=std::string::npos || tray.find("HandleMessage(HWND hwnd") == std::string::npos) return 6;
 if(app_cpp.find("(void)tray_.Initialize(this)")!=std::string::npos || app_cpp.find("if (!tray_.Initialize(this))") == std::string::npos) return 7;
 if(app_cpp.find("log_diagnostic") == std::string::npos) return 8;

 // 7f contract: native minimize must remain a real taskbar minimize. The SC_MINIMIZE
 // branch must not regress into HideToTray(), otherwise Windows loses per-window previews.
 const auto minimize = main.find("SC_MINIMIZE");
 if(minimize == std::string::npos) return 9;
 const auto minimize_end = main.find("case WM_CLOSE", minimize);
 if(minimize_end == std::string::npos) return 10;
 const auto minimize_block = main.substr(minimize, minimize_end - minimize);
 if(minimize_block.find("HideToTray") != std::string::npos) return 11;
 if(minimize_block.find("DefSubclassProc") == std::string::npos) return 12;

 // 7f contract: every MainWindow publishes its WinUI ActualTheme to its own native HWND
 // and keeps it synchronized; owned native dialogs then read that same DWM attribute.
 if(main.find("DwmSetWindowAttribute") == std::string::npos ||
    main.find("DWMWA_USE_IMMERSIVE_DARK_MODE") == std::string::npos) return 13;
 if(main.find("ActualThemeChanged") == std::string::npos ||
    main.find("sender.ActualTheme()") == std::string::npos) return 14;
 if(main.find("RootGrid().ActualTheme()") == std::string::npos) return 15;
 if(conflict.find("DwmGetWindowAttribute") == std::string::npos ||
    conflict.find("DWMWA_USE_IMMERSIVE_DARK_MODE") == std::string::npos) return 18;

 // Visible transfer windows start as normal switcher/taskbar windows; hiding is an
 // explicit tray operation, not a constructor side effect.
 if(window_cpp.find("app_window.IsShownInSwitchers(false)") != std::string::npos) return 16;
 if(main.find("AppWindow().IsShownInSwitchers(true)") == std::string::npos) return 17;

 // Tray activation can be re-entrant while WinUI constructs/registers the first window.
 // A physical click may yield multiple notification codes, so every tray/menu open must
 // funnel through one guarded dispatcher instead of calling App::ShowPrimaryWindow directly.
 if(tray_h.find("bool open_dispatch_active_{}") == std::string::npos ||
    tray_h.find("void OpenPrimaryWindow() noexcept") == std::string::npos) return 19;
 const auto open = tray.find("void AppTray::OpenPrimaryWindow() noexcept");
 if(open == std::string::npos) return 20;
 const auto open_end = tray.find("LRESULT AppTray::HandleMessage", open);
 if(open_end == std::string::npos) return 21;
 const auto open_block = tray.substr(open, open_end - open);
 if(open_block.find("if (open_dispatch_active_ || owner_ == nullptr) return") == std::string::npos ||
    open_block.find("open_dispatch_active_ = true") == std::string::npos ||
    open_block.find("owner_->ShowPrimaryWindow()") == std::string::npos ||
    open_block.find("open_dispatch_active_ = false") == std::string::npos) return 22;
 const auto direct_open = tray.find("owner_->ShowPrimaryWindow()");
 if(direct_open == std::string::npos || tray.find("owner_->ShowPrimaryWindow()", direct_open + 1) != std::string::npos) return 23;
 if(tray.find("OpenPrimaryWindow(); return 0;") == std::string::npos ||
    tray.find("if (command == kTrayOpenCommand) OpenPrimaryWindow();") == std::string::npos) return 24;
 return 0;
}
