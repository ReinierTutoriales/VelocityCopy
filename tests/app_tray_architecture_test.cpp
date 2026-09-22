#include <filesystem>
#include <fstream>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read(ui/"AppTray.cpp"), main=read(ui/"MainWindow.Tray.cpp"), app=read(ui/"App.xaml.h"), app_cpp=read(ui/"App.xaml.cpp");
 if(tray.find("Shell_NotifyIconW")==std::string::npos || tray.find("WS_EX_TOOLWINDOW")==std::string::npos) return 1;
 if(tray.find("HWND_MESSAGE")!=std::string::npos) return 4;
 if(tray.find("PostMessageW(hwnd_, WM_NULL") == std::string::npos) return 5;
 if(main.find("Shell_NotifyIconW")!=std::string::npos || main.find("NOTIFYICONDATA")!=std::string::npos) return 2;
 if(app.find("AppTray tray_")==std::string::npos) return 3;
 if(tray.find("DefWindowProcW(hwnd_")!=std::string::npos || tray.find("HandleMessage(HWND hwnd") == std::string::npos) return 6;
 if(app_cpp.find("(void)tray_.Initialize(this)")!=std::string::npos || app_cpp.find("if (!tray_.Initialize(this))") == std::string::npos) return 7;
 if(app_cpp.find("log_diagnostic") == std::string::npos) return 8;
 return 0;
}
