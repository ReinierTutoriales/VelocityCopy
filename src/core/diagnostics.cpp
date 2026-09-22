#include "velocitycopy/diagnostics.hpp"
#include "velocitycopy/app_storage.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <limits>
#include <string>
namespace velocitycopy { namespace {
constexpr std::uintmax_t kMaxLogBytes=256u*1024u;
std::wstring timestamp() noexcept {SYSTEMTIME v{};GetLocalTime(&v);try{return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}",v.wYear,v.wMonth,v.wDay,v.wHour,v.wMinute,v.wSecond,v.wMilliseconds);}catch(...){return L"unknown-time";}}
}
void log_diagnostic(std::wstring_view message) noexcept {try{
 const auto dir=app_data_directory();if(!dir)return;const auto path=*dir/L"VelocityCopy.log",rotated=*dir/L"VelocityCopy.log.1";std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
 if(!ec&&size>=kMaxLogBytes){std::filesystem::remove(rotated,ec);ec.clear();std::filesystem::rename(path,rotated,ec);}
 const auto text=std::format(L"[{}] {}\n",timestamp(),message);if(text.size()>static_cast<std::size_t>((std::numeric_limits<int>::max)()))return;
 const int chars=static_cast<int>(text.size());const int bytes=WideCharToMultiByte(CP_UTF8,0,text.data(),chars,nullptr,0,nullptr,nullptr);if(bytes<=0)return;
 std::string utf8(static_cast<std::size_t>(bytes),'\0');if(WideCharToMultiByte(CP_UTF8,0,text.data(),chars,utf8.data(),bytes,nullptr,nullptr)!=bytes)return;
 std::ofstream stream(path,std::ios::app|std::ios::binary);if(!stream)return;stream.write(utf8.data(),static_cast<std::streamsize>(utf8.size()));stream.flush();
}catch(...){}}
}
