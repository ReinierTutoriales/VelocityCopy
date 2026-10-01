#include "architecture_support.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <vector>
#include <string>
#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif
namespace {
std::vector<double> numbers(const std::string& text) {
    std::vector<double> values;
    static const std::regex number{R"re((\d+(?:\.\d+)?))re"};
    for (std::sregex_iterator it(text.begin(), text.end(), number), end; it != end; ++it)
        values.push_back(std::stod((*it)[1].str()));
    return values;
}
// A Thickness written as a single value ("0") means four equal sides.
std::vector<double> as_kind(std::vector<double> values, const std::string& kind) {
    if (kind == "thickness" && values.size() == 1) values.assign(4, values.front());
    return values;
}
}
namespace { bool contains(const std::string& t,const std::string& v){return t.find(v)!=std::string::npos;} int fail(int c,const char* m){std::cerr<<"ui token architecture contract "<<c<<": "<<m<<'\n';return c;} }
int main(){
 const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
 const auto tokens=read_source(root/"src/ui/DesignTokens.xaml");
 const auto accessor=read_source(root/"src/ui/VelocityCopy.UI/UiTokens.h");
 const auto xaml=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.xaml");
 if(tokens.empty()||accessor.empty()||xaml.empty()) return fail(1,"required UI token source missing");
 for(const auto* key:{"CompactSurfaceHeight","CompactWindowWidth","CaptionRowHeight","QueueExpandedMinHeight","QueueExpandedMaxHeight","QueueItemNameFontSize","QueueItemLocationFontSize","AboutWindowWidth","AboutWindowHeight"})
  if(!contains(tokens,std::string("x:Key=\"")+key+"\"")) return fail(2,"required token missing");
 if(!contains(accessor,"Application::Current().Resources().Lookup")||!contains(accessor,"token_double")||!contains(accessor,"token_thickness")||!contains(xaml,"Height=\"{StaticResource CompactSurfaceHeight}\"")||!contains(read_source(root/"src/ui/VelocityCopy.UI/MainWindow.xaml.cpp"),"token_double(L\"CaptionRowHeight\", 32)")) return fail(3,"XAML/C++ token bridge incomplete");
 for(const auto* name:{"MainWindow.xaml.cpp","MainWindow.Queue.cpp","MainWindow.Conflict.cpp","MainWindow.Execution.cpp","MainWindow.QueuePersistence.cpp"}){
  const auto source=read_source(root/"src/ui/VelocityCopy.UI"/name);
  if(contains(source,"ResizeWindow(72)")||contains(source,"ResizeWindow(72.0)")) return fail(4,"compact height literal escaped tokenization");
 }
 // Contract 5: every C++ token read names an existing key and its fallback is
 // the exact dictionary value, so a failed lookup can never change the UI.
 std::map<std::string, std::vector<double>> dictionary;
 static const std::regex entry{R"re(<(?:x:Double|Thickness|GridLength) x:Key="(\w+)">([^<]+)<)re"};
 for (std::sregex_iterator it(tokens.begin(), tokens.end(), entry), end; it != end; ++it)
  dictionary[(*it)[1].str()] = numbers((*it)[2].str());
 static const std::regex read{R"re(token_(double|int|thickness)\(L"(\w+)",\s*([^;]*?)\)\s*[;,)])re"};
 std::size_t checked = 0;
 for (const auto& entry_path : std::filesystem::directory_iterator(root / "src/ui/VelocityCopy.UI")) {
  if (entry_path.path().extension() != ".cpp") continue;
  const auto source = read_source(entry_path.path());
  const auto total = count_occurrences(source, "token_double(") + count_occurrences(source, "token_int(") +
                     count_occurrences(source, "token_thickness(");
  std::size_t matched = 0;
  for (std::sregex_iterator it(source.begin(), source.end(), read), end; it != end; ++it, ++matched) {
   const auto kind = (*it)[1].str();
   const auto key = (*it)[2].str();
   const auto found = dictionary.find(key);
   if (found == dictionary.end()) return fail(5, "C++ reads a token that DesignTokens.xaml does not define");
   if (as_kind(numbers((*it)[3].str()), kind) != as_kind(found->second, kind))
    return fail(6, "C++ token fallback differs from DesignTokens.xaml");
  }
  if (matched != total) return fail(7, "token read not in the verifiable token_x(L\"Key\", fallback) form");
  checked += matched;
 }
 if (checked == 0) return fail(8, "no C++ token reads found");
 return 0;
}
