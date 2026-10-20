#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/execution_control.hpp"
#include "destination_path_guard.hpp"
#include "win32_handle.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace velocitycopy {
namespace {
namespace fs = std::filesystem;

bool directory_identity(HANDLE handle, FileIdentity& identity) noexcept {
    std::error_code error;
    FILE_ID_INFO info{};
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!detail::safe_directory_handle(handle, error) ||
        !GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        !GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info))) return false;
    identity.volume_serial = info.VolumeSerialNumber;
    std::memcpy(identity.file_id.data(), info.FileId.Identifier, identity.file_id.size());
    return true;
}

bool same_identity(const FileIdentity& a, const FileIdentity& b) noexcept {
    return a.volume_serial == b.volume_serial && a.file_id == b.file_id;
}

fs::path normalized_absolute(const fs::path& path) { return fs::absolute(path).lexically_normal(); }

struct ExpectedEntry { bool directory; std::uint64_t size; };

// This is an admission check, not a filesystem transaction. The rename moves
// the selected directory object as a unit; a later namespace change travels
// with that object and is never followed for recursive copy or deletion.
bool matches_inventory(const CopyPlan& plan, const TreeMoveCandidate& candidate, ExecutionControl& control) {
    std::unordered_map<std::wstring, ExpectedEntry> expected;
    expected.reserve(plan.files.size() + plan.directories.size());
    const auto add = [&](const fs::path& source, const fs::path& target, bool directory, std::uint64_t size) {
        const auto relative = normalized_absolute(source).lexically_relative(candidate.source);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == L".." ||
            normalized_absolute(target) != (candidate.destination / relative).lexically_normal()) return false;
        if (relative == L".") return directory;
        return expected.emplace(relative.native(), ExpectedEntry{directory, size}).second;
    };
    for (const auto& directory : plan.directories)
        if (directory.source.empty() || !add(directory.source, directory.destination, true, 0)) return false;
    for (const auto& file : plan.files)
        if (!add(file.source, file.destination, false, file.size)) return false;

    std::vector<fs::path> pending{candidate.source};
    while (!pending.empty()) {
        if (control.directive() != ExecutionDirective::Run) return false;
        auto folder = std::move(pending.back());
        pending.pop_back();
        detail::DestinationPathGuard guard;
        std::error_code error;
        if (!guard.lock_existing_chain(folder, error) || !guard.missing.empty()) return false;
        WIN32_FIND_DATAW data{};
        struct Search {
            HANDLE handle;
            ~Search() { if (handle != INVALID_HANDLE_VALUE) FindClose(handle); }
        } search{FindFirstFileExW((folder / L"*").c_str(), FindExInfoBasic, &data,
            FindExSearchNameMatch, nullptr, 0)};
        if (search.handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) continue;
            return false;
        }
        do {
            if (control.directive() != ExecutionDirective::Run) return false;
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
            const auto path = folder / data.cFileName;
            const auto relative = path.lexically_relative(candidate.source);
            const auto found = expected.find(relative.native());
            const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const auto size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            if (found == expected.end() || found->second.directory != directory ||
                (!directory && found->second.size != size)) return false;
            expected.erase(found);
            if (directory) pending.push_back(path);
        } while (FindNextFileW(search.handle, &data));
        if (GetLastError() != ERROR_NO_MORE_FILES) return false;
    }
    return expected.empty();
}
} // namespace

void LiveCopyPlan::prepare_tree_move() noexcept {
    try {
        if (operation_ != FileOperation::Move || source_roots_.size() != 1 || directories_.empty() ||
            !retained_results_.empty() || directories_.front().source != source_roots_.front()) return;
        TreeMoveCandidate candidate{normalized_absolute(source_roots_.front()), normalized_absolute(directories_.front().destination), {}};
        if (candidate.source.native().find(L'\0') != std::wstring::npos ||
            candidate.destination.native().find(L'\0') != std::wstring::npos ||
            candidate.destination.parent_path() != normalized_absolute(destination_root_) ||
            candidate.source == candidate.source.root_path()) return;
        detail::Win32Handle source(CreateFileW(candidate.source.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (directory_identity(source.get(), candidate.identity)) tree_move_candidate_ = std::move(candidate);
    } catch (...) { /* Optional optimization; ordinary execution remains available. */ }
}

std::optional<TreeMoveCandidate> LiveCopyPlan::tree_move_candidate() const {
    std::lock_guard lock(mutex_);
    if (!tree_move_candidate_ || materialized_directory_count_ || !active_files_.empty() ||
        !parked_files_.empty() || !retained_results_.empty() || completed_files_ ||
        counters_.resolution_weight || counters_.bytes_written_physical ||
        pending_files_.size() != total_files_) return std::nullopt;
    return tree_move_candidate_;
}

bool LiveCopyPlan::try_move_tree(ExecutionControl& control) noexcept {
    try {
        const auto candidate = tree_move_candidate();
        if (!candidate) return false;
        const auto snapshot = export_remaining_plan();
        detail::DestinationPathGuard source_parents, destination_parents;
        std::error_code error;
        if (!source_parents.lock_existing_chain(candidate->source.parent_path(), error) ||
            !source_parents.missing.empty() ||
            !destination_parents.lock_existing_chain(candidate->destination.parent_path(), error) ||
            !destination_parents.missing.empty()) return false;
        if (GetFileAttributesW(candidate->destination.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND) return false;
        detail::Win32Handle inspected(CreateFileW(candidate->source.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FileIdentity inspected_id{}, destination_volume{};
        if (!directory_identity(inspected.get(), inspected_id) || !same_identity(inspected_id, candidate->identity) ||
            destination_parents.handles.empty() ||
            !directory_identity(destination_parents.handles.front().handle, destination_volume) ||
            inspected_id.volume_serial != destination_volume.volume_serial) return false;
        if (!matches_inventory(snapshot, *candidate, control)) return false;

        detail::Win32Handle source(CreateFileW(candidate->source.c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FileIdentity actual{};
        if (!directory_identity(source.get(), actual) || !same_identity(actual, candidate->identity)) return false;

        const auto& name = candidate->destination.native();
        const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
        auto storage = std::make_unique<std::byte[]>(bytes);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.get());
        rename->ReplaceIfExists = FALSE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::memcpy(rename->FileName, name.c_str(), (name.size() + 1) * sizeof(wchar_t));

        std::scoped_lock commit(mutex_, control.mutex_);
        if (!tree_move_candidate_ || materialized_directory_count_ || !active_files_.empty() ||
            !parked_files_.empty() || !retained_results_.empty() || completed_files_ ||
            counters_.resolution_weight || counters_.bytes_written_physical ||
            control.directive_ != ExecutionDirective::Run || control.has_pending_skips_.load()) return false;
        // FileRenameInfo never copies across volumes and never replaces the
        // destination. Its kernel result is authoritative even for cloned IDs.
        if (!SetFileInformationByHandle(source.get(), FileRenameInfo, rename, static_cast<DWORD>(bytes))) return false;

        // No allocation, callback or fallible operation after the rename.
        counters_.resolution_weight = counters_.resolution_total;
        counters_.bytes_succeeded = total_bytes_;
        outcomes_.succeeded = total_files_;
        completed_bytes_ = total_bytes_;
        completed_files_ = total_files_;
        pending_files_.clear();
        materialized_directory_count_ = directories_.size();
        tree_move_candidate_.reset();
        return true;
    } catch (...) { return false; }
}
} // namespace velocitycopy
