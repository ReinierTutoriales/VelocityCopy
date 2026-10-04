#include "architecture_support.hpp"
#include <filesystem>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
int main() {
 const auto root=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR};
 const auto conflict=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.Conflict.cpp");
 const auto execution=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
 const auto tray=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.Tray.cpp");
 const auto header=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.xaml.h");
 if(conflict.empty()||execution.empty()||tray.empty()||header.empty()) return 1;
 if(header.find("IAsyncOperation<std::uint32_t> decision_operation_") == std::string::npos) return 2;
 if(conflict.find("RequestDecisionAsync({") == std::string::npos ||
    execution.find("RequestDecisionAsync({") == std::string::npos) return 3;
 if(conflict.find("co_await velocitycopy::ui::show_decision_async") != std::string::npos ||
    execution.find("co_await velocitycopy::ui::show_decision_async") != std::string::npos) return 4;
 if(conflict.find("ShowNativeDecisionDialog") != std::string::npos ||
    execution.find("ShowNativeDecisionDialog") != std::string::npos) return 5;
 if(conflict.find("std::wstring detail") == std::string::npos ||
    conflict.find("decision.verification_checked") == std::string::npos) return 6;
 if(conflict.find("auto lifetime = get_strong()") == std::string::npos ||
    conflict.find("decision_queue_.push_back(request)") == std::string::npos ||
    conflict.find("SetEvent(decision_queue_.front()->turn)") == std::string::npos ||
    header.find("std::deque<std::shared_ptr<PendingDecision>> decision_queue_") == std::string::npos) return 7;
 if(tray.find("CancelDecisionQueue()") == std::string::npos ||
    conflict.find("request->cancelled.store") == std::string::npos) return 8;
 if(conflict.find("tray_exit_requested_ || session_ending_") == std::string::npos ||
    execution.find("tray_exit_requested_ || session_ending_") == std::string::npos) return 9;
 if(header.find("pending_conflict_") != std::string::npos || conflict.find("ShowPendingConflictDecision") != std::string::npos) return 10;
 if(conflict.find("DecisionTone::Warning") == std::string::npos ||
    execution.find("DecisionTone::Error") == std::string::npos) return 11;
 return 0;
}
