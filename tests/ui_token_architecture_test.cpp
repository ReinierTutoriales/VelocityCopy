#include "architecture_support.hpp"
#include <filesystem>
#include <cmath>
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
 for(const auto* key:{"NormalWindowMinWidth","CaptionRowHeight","QueueExpandedMinHeight","DetailsExpandedMinHeight","QueueExpandedMaxHeight","ActionButtonSize","ActionIconSize","CaptionFontSize","BodyFontSize","SubtitleFontSize","TransferProgressHeight","PerformanceGraphHeight","PerformanceAxisLabelMinWidth","AboutWindowWidth","AboutWindowHeight"})
  if(!contains(tokens,std::string("x:Key=\"")+key+"\"")) return fail(2,"required token missing");
 if(!contains(accessor,"Application::Current().Resources().Lookup")||!contains(accessor,"token_double")||!contains(accessor,"token_thickness")||!contains(read_source(root/"src/ui/VelocityCopy.UI/MainWindow.xaml.cpp"),"token_double(L\"CaptionRowHeight\", 32)")) return fail(3,"XAML/C++ token bridge incomplete");
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
  const auto normalized = std::regex_replace(source, std::regex{R"(\s+)"}, " ");
  std::size_t matched = 0;
  for (std::sregex_iterator it(normalized.begin(), normalized.end(), read), end; it != end; ++it, ++matched) {
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

 // Contract 9: StaticResource consumers must be type-compatible with the
 // declared XAML token. XAML compilation does not reliably reject a boxed
 // resource of the wrong type (for example x:Double -> CornerRadius), so this
 // architecture gate closes that runtime-only failure class.
 std::map<std::string,std::string> token_types;
 static const std::regex typed_token{R"re(<(x:Double|Thickness|GridLength|CornerRadius) x:Key="(\w+)">)re"};
 for(std::sregex_iterator it(tokens.begin(),tokens.end(),typed_token),end;it!=end;++it)
  token_types[(*it)[2].str()]=(*it)[1].str();
 auto static_resources_are_typed=[&](const std::string& markup) {
  static const std::regex use{R"re((Margin|Padding|CornerRadius|Width|Height|MinWidth|MinHeight|MaxWidth|MaxHeight|FontSize|Spacing|ColumnSpacing|RowSpacing)="\{StaticResource (\w+)\}")re"};
  for(std::sregex_iterator it(markup.begin(),markup.end(),use),end;it!=end;++it) {
   const auto property=(*it)[1].str();
   const auto key=(*it)[2].str();
   const auto found=token_types.find(key);
   if(found==token_types.end()) return false;
   const auto& type=found->second;
   if(property=="Margin"||property=="Padding") { if(type!="Thickness") return false; continue; }
   if(property=="CornerRadius") { if(type!="CornerRadius") return false; continue; }
   if(type!="x:Double" && type!="GridLength") return false;
  }
  return true;
 };
 for(const auto& entry_path:std::filesystem::directory_iterator(root/"src/ui/VelocityCopy.UI")) {
  if(entry_path.path().extension()==".xaml" && !static_resources_are_typed(read_source(entry_path.path())))
   return fail(9,"StaticResource token type is incompatible with its consuming XAML property");
 }
 auto bad_corner=xaml;
 const auto corner_anchor=std::string{"CornerRadius=\"{ThemeResource ControlCornerRadius}\""};
 const auto corner_pos=bad_corner.find(corner_anchor);
 if(corner_pos==std::string::npos) return fail(10,"performance graph CornerRadius contract anchor missing");
 bad_corner.replace(corner_pos,corner_anchor.size(),"CornerRadius=\"{StaticResource PerformanceGraphHeight}\"");
 if(static_resources_are_typed(bad_corner)) return fail(10,"typed StaticResource contract failed to reject Double -> CornerRadius mutation");

 // Contracts 20-26 are data invariants and are mutation-tested in memory.
 auto validate_design_tokens = [](const std::string& text) {
  std::map<std::string,std::vector<double>> values;
  static const std::regex e{R"re(<(?:x:Double|Thickness|GridLength) x:Key="(\w+)">([^<]+)<)re"};
  for(std::sregex_iterator it(text.begin(),text.end(),e),end;it!=end;++it) values[(*it)[1].str()]=numbers((*it)[2].str());
  auto scalar=[&](const char* key){auto it=values.find(key);return it==values.end()||it->second.empty()?-1.0:it->second.front();};
  for(const char* retired:{"SurfaceActionButtonSize","QueueCommandButtonSize","ActionRowHeight","BodyStrongFontSize","SkipIconSize","StopIconSize","CancelIconSize","DisclosureIconSize","QueueItemNameFontSize","QueueItemLocationFontSize"}) if(contains(text,retired)) return false;
  const auto icon=scalar("ActionIconSize"); if(icon!=16&&icon!=20&&icon!=24&&icon!=32) return false;
  auto pad=values.find("TransferContentPadding"); if(pad==values.end()||pad->second.size()!=4) return false;
  for(const auto& [key,vals]:values) if(key.ends_with("FontSize")) for(double v:vals) if(v!=12&&v!=14&&v!=20) return false;
  for(const auto& [key,vals]:values) {
   if(key.find("Opacity")!=std::string::npos||key.find("Radius")!=std::string::npos||
      key.find("FontSize")!=std::string::npos||key.ends_with("IconSize")) continue;
   for(double v:vals) if(std::fmod(v,4.0)!=0.0) return false;
  }
  if(scalar("TransferItemIconSize")!=20||scalar("DecisionStatusIconSize")!=20||
     scalar("SectionIconSize")!=16||scalar("QueueItemIconSize")!=16||
     scalar("InformationIconSize")!=16||scalar("PathArrowIconSize")!=12) return false;
  for(const auto& [key,vals]:values) if(key.ends_with("Opacity")) return false;
  if(scalar("CaptionRowGridLength")!=scalar("CaptionRowHeight")) return false;
  static const std::regex text_style{R"re(<Style x:Key="[^"]+" TargetType="TextBlock">[\s\S]*?<Setter Property="Foreground" Value="\{ThemeResource TextFillColor[^}]+\}"\s*/>[\s\S]*?</Style>)re"};
  return contains(text,"SecondaryTextStyle")&&contains(text,"TertiaryTextStyle")&&count_occurrences(text,"TargetType=\"TextBlock\"")==static_cast<std::size_t>(std::distance(std::sregex_iterator(text.begin(),text.end(),text_style),std::sregex_iterator{}));
 };
 if(!validate_design_tokens(tokens)) return fail(20,"design token invariants failed");
 auto mutate=[&](const std::string& from,const std::string& to){auto copy=tokens;auto pos=copy.find(from);if(pos==std::string::npos)return std::string{};copy.replace(pos,from.size(),to);return copy;};
 const std::vector<std::pair<std::string,std::string>> mutations={
  {"<x:Double x:Key=\"DecisionStatusIconSize\">20</x:Double>","<x:Double x:Key=\"DecisionStatusIconSize\">19</x:Double>"},
  {"<x:Double x:Key=\"SectionIconSize\">16</x:Double>","<x:Double x:Key=\"SectionIconSize\">15</x:Double>"},
  {"<x:Double x:Key=\"ActionIconSize\">16</x:Double>","<x:Double x:Key=\"ActionIconSize\">13</x:Double>"},
  {"<x:Double x:Key=\"ActionButtonSize\">32</x:Double>","<x:Double x:Key=\"ActionButtonSize\">32</x:Double><x:Double x:Key=\"QueueCommandButtonSize\">32</x:Double>"},
  {"<Thickness x:Key=\"QueueListMargin\">0,4,0,0</Thickness>","<Thickness x:Key=\"QueueListMargin\">0,7,0,0</Thickness>"},
  {"<x:Double x:Key=\"ActionButtonSize\">32</x:Double>","<x:Double x:Key=\"ActionButtonSize\">30</x:Double>"},
  {"<x:Double x:Key=\"ActionIconSize\">16</x:Double>","<x:Double x:Key=\"ActionIconSize\">16</x:Double><x:Double x:Key=\"QueueCountOpacity\">0.58</x:Double>"},
  {"<x:Double x:Key=\"BodyFontSize\">14</x:Double>","<x:Double x:Key=\"BodyFontSize\">14</x:Double><x:Double x:Key=\"BodyStrongFontSize\">14</x:Double>"},
  {"<x:Double x:Key=\"BodyFontSize\">14</x:Double>","<x:Double x:Key=\"BodyFontSize\">14</x:Double><x:Double x:Key=\"ExperimentalFontSize\">10.5</x:Double>"},
  {"<x:Double x:Key=\"ActionIconSize\">16</x:Double>","<x:Double x:Key=\"ActionIconSize\">16</x:Double><x:Double x:Key=\"AboutVersionOpacity\">0.6</x:Double>"},
  {"<x:Double x:Key=\"ActionButtonSize\">32</x:Double>","<x:Double x:Key=\"ActionButtonSize\">32</x:Double><x:Double x:Key=\"ActionRowHeight\">32</x:Double>"},
  {"<x:Double x:Key=\"ActionIconSize\">16</x:Double>","<x:Double x:Key=\"ActionIconSize\">16</x:Double><x:Double x:Key=\"CancelIconSize\">12</x:Double>"},
  {"Value=\"{ThemeResource TextFillColorSecondaryBrush}\"","Value=\"Red\""}
 };
 for(const auto& [from,to]:mutations){auto changed=mutate(from,to);if(changed.empty())return fail(26,"mutation anchor missing");if(validate_design_tokens(changed))return fail(26,"mutated design tokens unexpectedly accepted");}
 const auto queue=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
 const auto about=read_source(root/"src/ui/VelocityCopy.UI/MainWindow.About.cpp");
 if(contains(queue,"TextFillColorSecondaryBrush")||contains(about,"TextFillColorSecondaryBrush")||contains(about,"TextFillColorTertiaryBrush")||
    !contains(queue,"apply_text_style(location, L\"SecondaryTextStyle\")")||
    !contains(about,"apply_text_style(version_text, L\"SecondaryTextStyle\")")||
    !contains(about,"apply_text_style(metadata, L\"TertiaryTextStyle\")"))
  return fail(27,"C++ text must use theme-aware dictionary styles instead of captured brushes");
 return 0;
}
