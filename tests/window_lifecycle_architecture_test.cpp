#include <filesystem>
#include <fstream>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read(ui/"MainWindow.Tray.cpp"), exec=read(ui/"MainWindow.Execution.cpp"), app=read(ui/"App.xaml.cpp");
 if(tray.find("if (self->HasActiveTransfer()) self->HideToTray();")==std::string::npos) return 1;
 if(tray.find("else self->DestroyCompletedWindow();")==std::string::npos) return 2;
 if(exec.find("DestroyCompletedWindow();")==std::string::npos) return 3;
 if(app.find("main_window = winrt::make<MainWindow>();")==std::string::npos) return 4;
 if(app.find("OnWindowDestroyed")==std::string::npos) return 5;
 return 0;
}
