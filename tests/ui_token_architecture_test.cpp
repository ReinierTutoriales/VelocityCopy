#include "architecture_support.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
namespace { bool contains(const std::string& t,const std::string& v){return t.find(v)!=std::string::npos;} int fail(int c,const char* m){std::cerr<<"ui token architecture contract "<<c<<": "<<m<<'\n';return c;} }
int main(){
 const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
 const auto tokens=read_source(root/"src/ui/DesignTokens.xaml");
 const auto accessor=read_source(root/"src/ui/VelocityCopy.UI/UiTokens.h");
 const auto xaml=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.xaml");
 if(tokens.empty()||accessor.empty()||xaml.empty()) return fail(1,"required UI token source missing");
 for(const auto* key:{"CompactSurfaceHeight","CompactWindowWidth","CaptionRowHeight","QueueExpandedMinHeight","QueueExpandedMaxHeight","QueueItemNameFontSize","QueueItemLocationFontSize","AboutWindowWidth","AboutWindowHeight"})
  if(!contains(tokens,std::string("x:Key=\"")+key+"\"")) return fail(2,"required token missing");
 if(!contains(accessor,"Application::Current().Resources().Lookup")||!contains(accessor,"token_double")||!contains(accessor,"token_thickness")||!contains(xaml,"Height=\"{StaticResource CompactSurfaceHeight}\"")||!contains(read_source(root/"src/ui/VelocityCopy.UI/MainWindow.xaml.cpp"),"token_double(L\"CaptionRowHeight\", 34)")) return fail(3,"XAML/C++ token bridge incomplete");
 for(const auto* name:{"MainWindow.xaml.cpp","MainWindow.Queue.cpp","MainWindow.Conflict.cpp","MainWindow.Execution.cpp","MainWindow.QueuePersistence.cpp"}){
  const auto source=read_source(root/"src/ui/VelocityCopy.UI"/name);
  if(contains(source,"ResizeWindow(72)")||contains(source,"ResizeWindow(72.0)")) return fail(4,"compact height literal escaped tokenization");
 }
 return 0;
}
