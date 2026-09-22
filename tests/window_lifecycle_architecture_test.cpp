#include <filesystem>
#include <fstream>
#include <string>

std::string body_of(const std::string& text, const std::string& signature) {
 const auto start=text.find(signature); if(start==std::string::npos) return {};
 const auto open=text.find('{',start); if(open==std::string::npos) return {};
 int depth=0;
 for(std::size_t i=open;i<text.size();++i) {
  if(text[i]=='{') ++depth;
  else if(text[i]=='}') { --depth; if(depth==0) return text.substr(open,i-open+1); }
 }
 return {};
}
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read(ui/"MainWindow.Tray.cpp"), exec=read(ui/"MainWindow.Execution.cpp"), app=read(ui/"App.xaml.cpp");
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
 if(app.find("weak_ref<winrt::VelocityCopyUI::MainWindow>")!=std::string::npos) return 7;
 if(app.find("DeliverShellRequest(request)") == std::string::npos) return 8;
 for (const auto& entry : std::filesystem::directory_iterator(ui)) {
  if (!entry.is_regular_file()) continue;
  const auto name=entry.path().filename().string();
  if(name.rfind("MainWindow.",0)==0 && entry.path().extension()==".cpp" && read(entry.path()).find("DestroyWindow(hwnd_)")!=std::string::npos) return 9;
 }
 return 0;
}
