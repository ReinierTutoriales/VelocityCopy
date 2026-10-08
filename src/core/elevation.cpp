#include "velocitycopy/elevation.hpp"

#include "velocitycopy/app_storage.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shellapi.h>

#include <array>
#include <cwctype>
#include <vector>

// Consumers such as the WinUI project link only an umbrella import set.
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace velocitycopy {
namespace {

constexpr wchar_t kHandoffPrefix[] = L"VelocityCopy.Elevated.";
constexpr wchar_t kHandoffSuffix[] = L".vcq";

class Handle final {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (valid()) CloseHandle(value_);
    }
    [[nodiscard]] bool valid() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_;
};

// Case-insensitive, separator-normalized identity of a folder path.
std::wstring folder_key(const std::filesystem::path& path) {
    auto text = path.lexically_normal().wstring();
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::filesystem::path existing_ancestor(std::filesystem::path path) {
    for (int depth = 0; depth < 512 && !path.empty(); ++depth) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) return path;
        const auto parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

// Opens the folder asking for `right` (FILE_ADD_FILE / FILE_ADD_SUBDIRECTORY)
// so the file system itself decides; nothing is written. Returns true only for
// an explicit denial.
bool folder_right_denied(const std::filesystem::path& folder, const DWORD right) {
    Handle directory(CreateFileW(
        folder.c_str(), right, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    return !directory.valid() && GetLastError() == ERROR_ACCESS_DENIED;
}

// Elevation cannot help on a network destination: the elevated process does
// not see the person's mapped drives or use their network credentials.
bool is_remote(const std::filesystem::path& folder) {
    const auto text = folder.wstring();
    if (text.starts_with(L"\\\\")) return true;
    auto root = folder.root_path().wstring();
    if (root.empty()) return false;
    if (root.back() != L'\\') root.push_back(L'\\');
    return GetDriveTypeW(root.c_str()) == DRIVE_REMOTE;
}

std::optional<std::wstring> sha256_of(HANDLE file) {
    LARGE_INTEGER origin{};
    if (!SetFilePointerEx(file, origin, nullptr, FILE_BEGIN)) return std::nullopt;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return std::nullopt;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::wstring> result;
    if (BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> buffer(1u << 16);
        bool ok = true;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                ok = false;
                break;
            }
            if (read == 0) break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) {
                ok = false;
                break;
            }
        }
        std::array<unsigned char, 32> digest{};
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
            constexpr wchar_t kHex[] = L"0123456789abcdef";
            std::wstring text;
            text.reserve(digest.size() * 2);
            for (const auto byte : digest) {
                text.push_back(kHex[byte >> 4]);
                text.push_back(kHex[byte & 0x0F]);
            }
            result = std::move(text);
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

bool same_digest(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != 64 || right.size() != 64) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) return false;
    }
    return true;
}

// No write/delete sharing: while this handle is open the content cannot
// change between hashing and parsing.
std::optional<QueueArchive> load_verified(const std::filesystem::path& file, std::wstring_view expected_sha256) {
    Handle hold(CreateFileW(
        file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!hold.valid()) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION details{};
    if (!GetFileInformationByHandle(hold.get(), &details) ||
        (details.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
        return std::nullopt;
    }
    const auto digest = sha256_of(hold.get());
    if (!digest || !same_digest(*digest, expected_sha256)) return std::nullopt;
    return QueueArchiveStore{}.load(file);
}

} // namespace

bool process_is_elevated() noexcept {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
    Handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) &&
        elevation.TokenIsElevated != 0;
}

bool destination_requires_elevation(const CopyPlan& plan) noexcept {
    try {
        if (plan.destination_root.empty() || process_is_elevated()) return false;
        const auto anchor = existing_ancestor(plan.destination_root);
        if (anchor.empty() || is_remote(anchor)) return false;

        // The first thing written into the existing folder decides which
        // permission matters: the destination root itself (a folder), or the
        // files and folders placed directly in an existing root.
        bool needs_file = false;
        bool needs_folder = false;
        const auto anchor_key = folder_key(anchor);
        if (anchor_key != folder_key(plan.destination_root)) {
            needs_folder = true;
        } else {
            for (const auto& file : plan.files) {
                if (folder_key(file.destination.parent_path()) == anchor_key) {
                    needs_file = true;
                    break;
                }
            }
            for (const auto& directory : plan.directories) {
                if (folder_key(directory.destination.parent_path()) == anchor_key) {
                    needs_folder = true;
                    break;
                }
            }
        }
        return (needs_file && folder_right_denied(anchor, FILE_ADD_FILE)) ||
            (needs_folder && folder_right_denied(anchor, FILE_ADD_SUBDIRECTORY));
    } catch (...) {
        return false;
    }
}

std::optional<ElevatedHandoff> write_elevated_handoff(
    const std::filesystem::path& directory,
    const QueueArchive& archive) noexcept {
    try {
        const auto id = new_session_id();
        if (id.empty()) return std::nullopt;
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        const auto file = directory / (std::wstring(kHandoffPrefix) + id + kHandoffSuffix);
        if (!QueueArchiveStore{}.save(file, archive)) return std::nullopt;

        Handle stream(CreateFileW(
            file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        auto digest = stream.valid() ? sha256_of(stream.get()) : std::nullopt;
        if (!digest) {
            (void)DeleteFileW(file.c_str());
            return std::nullopt;
        }
        return ElevatedHandoff{file, std::move(*digest)};
    } catch (...) {
        return std::nullopt;
    }
}

std::int32_t launch_elevated_handoff(
    const std::filesystem::path& executable,
    const ElevatedHandoff& handoff,
    void* owner_window) noexcept {
    std::int32_t result = static_cast<std::int32_t>(E_FAIL);
    try {
        // ShellExecuteEx may hand the request to shell extensions.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const std::wstring parameters =
            L"--elevated-handoff \"" + handoff.file.wstring() + L"\" " + handoff.sha256;
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.hwnd = static_cast<HWND>(owner_window);
        info.lpVerb = L"runas";
        info.lpFile = executable.c_str();
        info.lpParameters = parameters.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) {
            if (info.hProcess != nullptr) CloseHandle(info.hProcess);
            result = S_OK;
        } else {
            result = static_cast<std::int32_t>(HRESULT_FROM_WIN32(GetLastError()));
        }
        if (SUCCEEDED(com)) CoUninitialize();
    } catch (...) {
    }
    if (result != S_OK) (void)DeleteFileW(handoff.file.c_str());
    return result;
}

std::optional<QueueArchive> take_elevated_handoff(
    const std::filesystem::path& file,
    const std::wstring_view expected_sha256) noexcept {
    std::optional<QueueArchive> archive;
    try {
        const auto name = file.filename().wstring();
        if (!name.starts_with(kHandoffPrefix) || !name.ends_with(kHandoffSuffix)) return std::nullopt;
        archive = load_verified(file, expected_sha256);
    } catch (...) {
        archive.reset();
    }
    // After load_verified closed its handle (it denies delete sharing).
    (void)DeleteFileW(file.c_str());
    return archive;
}

} // namespace velocitycopy
