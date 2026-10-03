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
 if(h.empty()||c.empty()) return 1;
 if(h.find("IAsyncOperation<std::int32_t> show_decision_async")==std::string::npos) return 2;
 if(c.find("resume_on_signal")==std::string::npos || c.find("apartment_context")==std::string::npos) return 3;
 if(c.find("compare_exchange_strong")==std::string::npos) return 4;
 if(c.find("DecisionChoice::Cancel") == std::string::npos || c.find("dialog.Closed(")==std::string::npos) return 5;
 if(c.find("AccentButtonStyle")==std::string::npos || c.find("AutomationProperties::SetName")==std::string::npos) return 6;
 if(c.find("VirtualKey::Escape")==std::string::npos || c.find("VirtualKey::Enter")==std::string::npos) return 7;
 if(c.find("TaskDialog")!=std::string::npos || c.find("DarkMode_Explorer")!=std::string::npos || c.find("SetWindowTheme")!=std::string::npos) return 8;
 if(c.find("EnableWindow(")!=std::string::npos) return 9;
 return 0;
}
