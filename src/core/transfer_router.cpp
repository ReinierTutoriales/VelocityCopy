#include "velocitycopy/transfer_router.hpp"
#include "velocitycopy/storage_profiler.hpp"
#include <algorithm>
#include <cwctype>
namespace velocitycopy { namespace {
std::wstring path_key(const std::filesystem::path& p){auto s=p.lexically_normal().wstring();std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});while(s.size()>3&&(s.back()==L'\\'||s.back()==L'/'))s.pop_back();return s;}
bool same_destination(const std::filesystem::path&a,const std::filesystem::path&b){return !a.empty()&&!b.empty()&&path_key(a)==path_key(b);}
bool shares_device(const TransferRequest&r,const ActiveSession&s) noexcept {return same_device(r.destination,s.destination)||same_device(r.destination,s.source)||same_device(r.source,s.destination)||same_device(r.source,s.source);}
}
std::wstring fallback_volume_key(const std::filesystem::path& path) noexcept {
 auto s=path.lexically_normal().wstring();
 std::replace(s.begin(),s.end(),L'/',L'\\');
 std::transform(s.begin(),s.end(),s.begin(),[](wchar_t ch){return static_cast<wchar_t>(std::towlower(ch));});
 if(s.size()>=2&&s[0]==L'\\'&&s[1]==L'\\'){
  const auto server_end=s.find(L'\\',2);
  if(server_end!=std::wstring::npos){
   const auto share_end=s.find(L'\\',server_end+1);
   return share_end==std::wstring::npos?s:s.substr(0,share_end);
  }
  return s;
 }
 if(s.size()>=2&&s[1]==L':') return s.substr(0,2)+L"\\";
 return {};
}
StorageKey resolve_storage_key(const std::filesystem::path& path) noexcept {
 const auto profile=StorageProfiler{}.inspect(path);
 StorageKey key{};
 key.volume=profile.volume_id.empty()?fallback_volume_key(profile.volume_root.empty()?path:profile.volume_root):profile.volume_id;
 // A StorageKey has room for one physical disk only. Publish it only when
 // Windows reports a fixed volume backed by exactly one physical disk;
 // spanned/striped volumes and non-fixed media deliberately remain unknown.
 if(profile.kind==StorageKind::Fixed&&profile.physical_disk_extents_available&&profile.physical_disk_numbers.size()==1)
  key.disk=profile.physical_disk_numbers.front();
 return key;
}
bool same_device(const StorageKey&a,const StorageKey&b) noexcept {
 if(!a.volume.empty()&&!b.volume.empty()){auto x=a.volume,y=b.volume;std::transform(x.begin(),x.end(),x.begin(),::towlower);std::transform(y.begin(),y.end(),y.begin(),::towlower);if(x==y)return true;}
 return a.disk&&b.disk&&*a.disk==*b.disk;
}
RouteResult route_transfer(const TransferRequest&r,std::span<const ActiveSession> sessions,const RoutePreferences&p){
 const ActiveSession* best=nullptr;
 for(const auto&s:sessions) if(same_destination(r.destination_root,s.destination_root)&&r.operation==s.operation&&s.accepting_appends&&(best==nullptr||s.window_id<best->window_id)) best=&s;
 if(best){
  if(p.same_destination==RouteChoice::Append)return {RouteDecision::AppendTo,best->window_id,{},RouteChoice::Append};
  if(p.same_destination==RouteChoice::Wait)return {RouteDecision::WaitFor,best->window_id,{},RouteChoice::Append};
  return {RouteDecision::Ask,best->window_id,{RouteChoice::Append,RouteChoice::Wait},RouteChoice::Append};
 }
 best=nullptr;
 for(const auto&s:sessions) if(shares_device(r,s)&&(best==nullptr||s.window_id<best->window_id)) best=&s;
 if(best){
  if(p.same_device==RouteChoice::Wait)return {RouteDecision::WaitFor,best->window_id,{},RouteChoice::Wait};
  if(p.same_device==RouteChoice::Parallel)return {RouteDecision::StartNew,0,{},RouteChoice::Wait};
  return {RouteDecision::Ask,best->window_id,{RouteChoice::Wait,RouteChoice::Parallel},RouteChoice::Wait};
 }
 return {RouteDecision::StartNew,0,{},RouteChoice::Parallel};
}
}
