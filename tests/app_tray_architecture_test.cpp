#include <filesystem>
#include <fstream>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read(ui/"AppTray.cpp"), main=read(ui/"MainWindow.Tray.cpp"), app=read(ui/"App.xaml.h"), app_cpp=read(ui/"App.xaml.cpp"), window_cpp=read(ui/"MainWindow.xaml.cpp");
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

 // 7f contract: every MainWindow must publish its WinUI ActualTheme to its native HWND,
 // and keep it synchronized so owned TaskDialogs/system chrome inherit the correct theme.
 if(main.find("DwmSetWindowAttribute") == std::string::npos ||
    main.find("DWMWA_USE_IMMERSIVE_DARK_MODE") == std::string::npos) return 13;
 if(main.find("ActualThemeChanged") == std::string::npos ||
    main.find("sender.ActualTheme()") == std::string::npos) return 14;
 if(main.find("RootGrid().ActualTheme()") == std::string::npos) return 15;

 // Visible transfer windows start as normal switcher/taskbar windows; hiding is an
 // explicit tray operation, not a constructor side effect.
 if(window_cpp.find("app_window.IsShownInSwitchers(false)") != std::string::npos) return 16;
 if(main.find("AppWindow().IsShownInSwitchers(true)") == std::string::npos) return 17;
 return 0;
}
