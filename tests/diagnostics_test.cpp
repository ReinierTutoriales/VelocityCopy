#include "velocitycopy/diagnostics.hpp"
#include "velocitycopy/app_storage.hpp"
#include <filesystem>
#include <fstream>
#include <string>
int main(){
 const auto dir=velocitycopy::app_data_directory();if(!dir)return 1;const auto log=*dir/L"VelocityCopy.log",old=*dir/L"VelocityCopy.log.1";std::error_code ec;std::filesystem::remove(log,ec);std::filesystem::remove(old,ec);
 velocitycopy::log_diagnostic(L"diagnostics-test-line");velocitycopy::log_diagnostic(L"Música ñ");
 {std::ifstream in(log,std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),{});if(data.find("diagnostics-test-line")==std::string::npos)return 2;if(data.find("M\xC3\xBAsica \xC3\xB1")==std::string::npos)return 5;}
 {std::ofstream out(log,std::ios::binary|std::ios::trunc);std::string block(256u*1024u,'x');out.write(block.data(),static_cast<std::streamsize>(block.size()));}
 velocitycopy::log_diagnostic(L"after-rotation");if(!std::filesystem::exists(old)||!std::filesystem::exists(log))return 3;
 {std::ifstream in(log,std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),{});if(data.find("after-rotation")==std::string::npos)return 4;}
 std::filesystem::remove(log,ec);std::filesystem::create_directory(log,ec);velocitycopy::log_diagnostic(L"must-not-throw");std::filesystem::remove_all(log,ec);std::filesystem::remove(old,ec);return 0;
}
