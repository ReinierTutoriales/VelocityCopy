#include <filesystem>
#include <fstream>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read(ui/"AppTray.cpp"), main=read(ui/"MainWindow.Tray.cpp"), app=read(ui/"App.xaml.h");
 if(tray.find("Shell_NotifyIconW")==std::string::npos || tray.find("HWND_MESSAGE")==std::string::npos) return 1;
 if(main.find("Shell_NotifyIconW")!=std::string::npos || main.find("NOTIFYICONDATA")!=std::string::npos) return 2;
 if(app.find("AppTray tray_")==std::string::npos) return 3;
 return 0;
}
