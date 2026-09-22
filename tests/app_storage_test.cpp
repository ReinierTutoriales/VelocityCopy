#include "velocitycopy/app_storage.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::wstring id = L"{01234567-89AB-CDEF-8123-456789ABCDEF}";
    const auto valid = velocitycopy::recovery_session_id(
        std::filesystem::path(L"VelocityCopy.Recovery.") += id + L".vcq");
    if (!valid || *valid != id) return 1;
    if (velocitycopy::recovery_session_id(L"VelocityCopy.Recovery.{bad}.vcq")) return 2;
    if (velocitycopy::recovery_session_id(L"Other.Recovery.{01234567-89AB-CDEF-8123-456789ABCDEF}.vcq")) return 3;
    if (velocitycopy::recovery_session_id(L"VelocityCopy.Recovery.vcq")) return 4;
    if (velocitycopy::recovery_session_id(L"VelocityCopy.Recovery.{01234567-89AB-CDEF-8123-456789ABCDEF}.vcq.tmp")) return 5;
    if (velocitycopy::recovery_session_id(L"VelocityCopy.Recovery.{01234567-89AB-CDEF-8123-456789ABCDEF}.vcq.consumed")) return 6;

    const auto root = std::filesystem::temp_directory_path() / velocitycopy::new_session_id();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) return 7;
    const auto recovery = velocitycopy::recovery_file(root, id);
    const auto orphan = std::filesystem::path(recovery.wstring() + L".tmp");
    { std::ofstream out(recovery); out << "valid"; }
    { std::ofstream out(orphan); out << "orphan"; }
    { std::ofstream out(root / L"garbage.vcq"); out << "garbage"; }

    const auto files = velocitycopy::list_recovery_files(root);
    if (files.size() != 1 || files.front() != recovery) return 8;
    if (std::filesystem::exists(orphan)) return 9;

    velocitycopy::retire_recovery_file(recovery);
    if (std::filesystem::exists(recovery)) return 10;
    std::filesystem::remove_all(root, ec);
    return 0;
}
