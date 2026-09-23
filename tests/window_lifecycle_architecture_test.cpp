#include "architecture_support.hpp"
#include <filesystem>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string block_from(const std::string& text, const std::string& marker) {
 const auto marker_pos=text.find(marker);
 if(marker_pos==std::string::npos) return {};
 const auto open=text.find('{', marker_pos);
 if(open==std::string::npos) return {};
 int depth=0;
 for(std::size_t i=open;i<text.size();++i){
  if(text[i]=='{') ++depth;
  else if(text[i]=='}' && --depth==0) return text.substr(open+1,i-open-1);
 }
 return {};
}
}

int main(){
 const auto ui=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR}/"src/ui/VelocityCopy.UI";
 const auto tray=read_source(ui/"MainWindow.Tray.cpp"), exec=read_source(ui/"MainWindow.Execution.cpp"), app=read_source(ui/"App.xaml.cpp");
 if(tray.find("self->HasActiveTransfer()") == std::string::npos || tray.find("self->CancelAndCloseWindow();") == std::string::npos) return 1;
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
 const auto destroy=finish.find("DestroyCompletedWindow();");
 if(destroy == std::string::npos) return 13;
 if(finish.find("HasPendingRecovery(") != std::string::npos ||
    finish.find("MaybeOfferRecoveryAsync()") != std::string::npos) return 18;

 const auto cancelled=block_from(finish,"if (result.cancelled)");
 if(cancelled.empty() || cancelled.find("queued_sessions_.clear()") != std::string::npos ||
    cancelled.find("if (queued_sessions_.empty()) DestroyCompletedWindow();") == std::string::npos ||
    cancelled.find("else StartNextQueuedSession();") == std::string::npos) return 23;
 const auto failed=block_from(finish,"if (!result.success)");
 if(failed.empty() || failed.find("ShowError(reason);") == std::string::npos ||
    failed.find("if (!queued_sessions_.empty())") == std::string::npos ||
    failed.find("StartNextQueuedSession();") == std::string::npos ||
    failed.find("DestroyCompletedWindow();") != std::string::npos) return 24;
 const auto cancel_session=body_of(exec,"void MainWindow::CancelCurrentSession(");
 if(cancel_session.empty() || cancel_session.find("queued_sessions_.clear()") != std::string::npos) return 25;
 const auto active_cancel=block_from(cancel_session,"if (execution_control_)");
 if(active_cancel.empty() || active_cancel.find("request_cancel();") == std::string::npos ||
    active_cancel.find("StartNextQueuedSession();") != std::string::npos ||
    active_cancel.find("DestroyCompletedWindow();") != std::string::npos) return 26;
 const auto attention_cancel=block_from(cancel_session,"if (stopped_session_ || conflict_session_)");
 if(attention_cancel.empty() || attention_cancel.find("if (queued_sessions_.empty()) DestroyCompletedWindow();") == std::string::npos ||
    attention_cancel.find("else StartNextQueuedSession();") == std::string::npos) return 27;
 const auto stopped_finalizer=body_of(exec,"void MainWindow::FinalizeStoppedSessionIfEmpty()");
 if(stopped_finalizer.empty() ||
    stopped_finalizer.find("if (queued_sessions_.empty()) DestroyCompletedWindow();") == std::string::npos ||
    stopped_finalizer.find("else StartNextQueuedSession();") == std::string::npos) return 28;

 const auto show_from_tray=body_of(tray,"void MainWindow::ShowFromTray(");
 if(show_from_tray.empty() || show_from_tray.find("MaybeOfferRecoveryAsync()") != std::string::npos) return 19;
 const auto primary=body_of(app,"void App::ShowPrimaryWindow(");
 if(primary.empty() || primary.find("implementation->ShowFromTray();") == std::string::npos ||
    primary.find("implementation->OfferRecoveryIfIdle();") == std::string::npos) return 20;
 const auto deliver_job=body_of(app,"void App::DeliverConvertedJob(");
 if(deliver_job.empty() || deliver_job.find("OfferRecoveryIfIdle") != std::string::npos) return 21;
 const auto launched=body_of(app,"void App::OnLaunched(");
 if(launched.empty() || launched.find("if (!initial_request)") == std::string::npos ||
    launched.find("implementation->OfferRecoveryIfIdle();") == std::string::npos) return 22;
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
