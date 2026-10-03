#include "architecture_support.hpp"
#include <filesystem>
#include <iostream>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
int main() {
 const auto root=std::filesystem::path{VELOCITYCOPY_SOURCE_DIR};
 const auto h=read_source(root/"src/ui/VelocityCopy.UI/DecisionSurface.h");
 const auto c=read_source(root/"src/ui/VelocityCopy.UI/DecisionSurface.cpp");
 const auto tokens=read_source(root/"src/ui/DesignTokens.xaml");
 if(h.empty()||c.empty()||tokens.empty()) return 1;
 if(h.find("IAsyncOperation<std::uint32_t> show_decision_async") == std::string::npos ||
    h.find("DecisionOptions options") == std::string::npos || h.find("const DecisionOptions&") != std::string::npos) return 2;
 if(h.find("bool*") != std::string::npos || c.find("bool*") != std::string::npos ||
    h.find("encode_decision") == std::string::npos || h.find("decode_decision") == std::string::npos) return 3;
 if(c.find("resume_on_signal")==std::string::npos || c.find("apartment_context")==std::string::npos) return 4;
 if(c.find("compare_exchange_strong")==std::string::npos) return 5;
 if(c.find("get_cancellation_token") == std::string::npos || c.find("cancellation.callback(") == std::string::npos ||
    c.find("cancellation.enable_propagation(false)") == std::string::npos) return 6;
 if(c.find("DecisionChoice::Cancel") == std::string::npos || c.find("dialog.Closed(")==std::string::npos) return 7;
 if(c.find("AccentButtonStyle")==std::string::npos || c.find("AutomationProperties::SetName")==std::string::npos) return 8;
 if(c.find("VirtualKey::Enter") != std::string::npos || c.find("root.KeyDown") != std::string::npos ||
    c.find("VirtualKey::Escape")==std::string::npos || c.find("KeyboardAccelerator") == std::string::npos) return 9;
 if(c.find("root.Loaded(") == std::string::npos || c.find("primary.Focus(FocusState::Programmatic)") == std::string::npos) return 10;
 if(c.find("root.Measure(") == std::string::npos || c.find("root.DesiredSize().Height") == std::string::npos ||
    tokens.find("DecisionWindowHeight") != std::string::npos) return 11;
 if(c.find("ExtendsContentIntoTitleBar(true)") == std::string::npos || c.find("SetTitleBar(title_bar)") == std::string::npos) return 12;
 if(c.find("MonitorFromWindow") == std::string::npos || c.find("GetMonitorInfoW") == std::string::npos ||
    c.find("monitor_info.rcWork") == std::string::npos) return 13;
 if(c.find("TaskDialog")!=std::string::npos || c.find("DarkMode_Explorer")!=std::string::npos || c.find("SetWindowTheme")!=std::string::npos) return 14;
 if(c.find("EnableWindow(")!=std::string::npos) return 15;
 return 0;
}
