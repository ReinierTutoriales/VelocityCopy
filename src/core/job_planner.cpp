#include "velocitycopy/job_planner.hpp"
#include "destination_path_guard.hpp"
#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/path_key.hpp"
#include "velocitycopy/execution_control.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>
#include <map>
#include <set>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace velocitycopy {
namespace {

constexpr std::size_t kMaxPlannedEntries = 250'000;

class DirectorySearch final {
public:
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~DirectorySearch() { if (handle != INVALID_HANDLE_VALUE) FindClose(handle); }
    DirectorySearch() = default;
    DirectorySearch(const DirectorySearch&) = delete;
    DirectorySearch& operator=(const DirectorySearch&) = delete;
};

void ensure_plan_capacity(const CopyPlan& plan, const std::filesystem::path& path) {
    if (plan.directories.size() + plan.files.size() >= kMaxPlannedEntries) {
        throw std::filesystem::filesystem_error(
            "Copy plan exceeds the supported entry limit",
            path,
            std::make_error_code(std::errc::value_too_large));
    }
}

std::filesystem::path destination_root_for(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    DestinationLayout layout,
    const std::filesystem::file_status& status,
    bool disambiguate_by_parent) {
    const bool is_directory = std::filesystem::is_directory(status);
    const bool is_regular_file = std::filesystem::is_regular_file(status);

    if (layout == DestinationLayout::ContentsOnly) {
        if (is_directory) {
            return destination;
        }
        if (is_regular_file) {
            return destination / source.filename();
        }
    }

    if (is_regular_file && disambiguate_by_parent) {
        // Loose files land directly in the destination, exactly like Explorer:
        // selecting a.txt and b.txt in C:\Fotos and copying them to D:\ yields
        // D:\a.txt and D:\b.txt, never D:\Fotos\a.txt. A synthetic parent
        // folder is invented only for the rare file whose name collides with
        // another top-level source of the same job (two "same.txt" from
        // different folders, or a file named like a selected folder), because
        // OutputRegistry would otherwise reject the whole job.
        const auto immediate_parent = source.parent_path().filename();
        if (!immediate_parent.empty()) {
            return destination / immediate_parent / source.filename();
        }
    }

    return destination / source.filename();
}

[[noreturn]] void throw_collision(const std::filesystem::path& path) {
    throw std::filesystem::filesystem_error(
        "Multiple copy entries resolve to the same destination",
        path,
        std::make_error_code(std::errc::file_exists));
}

void throw_if_cancelled(const std::stop_token stop_token, ExecutionControl* control) {
    auto directive = control ? control->directive() : ExecutionDirective::Run;
    if (directive == ExecutionDirective::Pause) directive = control->wait_while_paused(stop_token);
    if (stop_token.stop_requested() || directive != ExecutionDirective::Run) {
        throw std::system_error(std::make_error_code(std::errc::operation_canceled));
    }
}

void validate_source_reparse_semantics(const std::filesystem::path& path) {
    const HANDLE handle = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        throw std::filesystem::filesystem_error(
            "Unable to inspect source reparse metadata",
            path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }

    FILE_ATTRIBUTE_TAG_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) == 0) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        throw std::filesystem::filesystem_error(
            "Unable to inspect source reparse metadata",
            path,
            std::error_code(static_cast<int>(error), std::system_category()));
    }
    CloseHandle(handle);

    if ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
        IsReparseTagNameSurrogate(info.ReparseTag) != FALSE) {
        throw std::filesystem::filesystem_error(
            "Name-surrogate source reparse points are not supported",
            path,
            std::error_code(ERROR_CANT_ACCESS_FILE, std::system_category()));
    }
}

class OutputRegistry final {
public:
    explicit OutputRegistry(PathKeyFactory& keys) : keys_(keys) {}
    void add_directory(const std::filesystem::path& path) {
        auto cursor = path;
        while (!cursor.empty()) {
            const auto key = keys_.make(cursor);
            if (key.empty()) {
                throw std::filesystem::filesystem_error(
                    "Unable to normalize destination path",
                    cursor,
                    std::make_error_code(std::errc::invalid_argument));
            }
            if (files_.contains(key)) {
                throw_collision(cursor);
            }
            // Registering a directory also registers its complete ancestor
            // chain. Once an ancestor is already known, walking higher would
            // only normalize and hash paths that are already reserved.
            if (!directories_.insert(key).second) {
                break;
            }

            const auto parent = cursor.parent_path();
            if (parent.empty() || parent == cursor) {
                break;
            }
            cursor = parent;
        }
    }

    void add_file(const std::filesystem::path& path) {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            add_directory(parent);
        }

        const auto key = keys_.make(path);
        if (key.empty()) {
            throw std::filesystem::filesystem_error(
                "Unable to normalize destination path",
                path,
                std::make_error_code(std::errc::invalid_argument));
        }
        if (directories_.contains(key) || !files_.insert(key).second) {
            throw_collision(path);
        }
    }

private:
    PathKeyFactory& keys_;
    std::set<std::wstring, PathKeyLess> files_;
    std::set<std::wstring, PathKeyLess> directories_;
};

void checked_add(std::uint64_t& total, const std::uint64_t value, const std::filesystem::path& path) {
    if (value > std::numeric_limits<std::uint64_t>::max() - total) {
        throw std::filesystem::filesystem_error(
            "Copy size exceeds supported range",
            path,
            std::make_error_code(std::errc::value_too_large));
    }
    total += value;
}

[[noreturn]] void throw_unsupported(const std::filesystem::path& path) {
    throw std::filesystem::filesystem_error(
        "Unsupported source type",
        path,
        std::make_error_code(std::errc::not_supported));
}

bool valid_relative_path(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == L"..") {
            return false;
        }
    }
    return true;
}

void record_failure(
    CopyPlan& plan,
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const std::error_code& code) {
    if (plan.failures.size() >= kMaxPlannedEntries) {
        throw std::filesystem::filesystem_error(
            "Copy plan exceeds the supported entry limit",
            source,
            std::make_error_code(std::errc::value_too_large));
    }
    plan.failures.push_back({source, destination, planning_error_hresult(code)});
}

void account_file(CopyPlan& plan, const PlannedFile& file) {
    checked_add(plan.total_bytes, file.size, file.source);
    plan.largest_file_bytes = std::max(plan.largest_file_bytes, file.size);
}

} // namespace

std::int32_t planning_error_hresult(const std::error_code& code) noexcept {
    if (!code) {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    }
    if (code.category() == std::system_category()) {
        return static_cast<std::int32_t>(HRESULT_FROM_WIN32(static_cast<DWORD>(code.value())));
    }

    DWORD native = ERROR_INVALID_DATA;
    if (code == std::errc::file_exists) native = ERROR_FILE_EXISTS;
    else if (code == std::errc::invalid_argument) native = ERROR_INVALID_PARAMETER;
    else if (code == std::errc::value_too_large) native = ERROR_ARITHMETIC_OVERFLOW;
    else if (code == std::errc::not_supported) native = ERROR_NOT_SUPPORTED;
    else if (code == std::errc::operation_canceled) native = ERROR_REQUEST_ABORTED;
    else if (code == std::errc::no_such_file_or_directory) native = ERROR_FILE_NOT_FOUND;
    else if (code == std::errc::permission_denied) native = ERROR_ACCESS_DENIED;
    else if (code == std::errc::not_enough_memory) native = ERROR_NOT_ENOUGH_MEMORY;
    return static_cast<std::int32_t>(HRESULT_FROM_WIN32(native));
}

CopyPlan JobPlanner::build(const CopyJob& job) const {
    return build(job, {});
}

CopyPlan JobPlanner::build(const CopyJob& job, const std::stop_token stop_token) const {
    return build_impl(job, stop_token, nullptr, 0, nullptr);
}

void JobPlanner::stream(const CopyJob& job, ExecutionControl& control, const std::size_t batch_entries,
    const BatchSink& sink, const std::stop_token stop_token) const {
    if (!sink || batch_entries == 0 || batch_entries > kMaxPlannedEntries || job.operation != FileOperation::Copy)
        throw std::system_error(std::make_error_code(std::errc::invalid_argument));
    (void)build_impl(job, stop_token, &sink, batch_entries, &control);
}

CopyPlan JobPlanner::build_impl(const CopyJob& job, const std::stop_token stop_token,
    const BatchSink* sink, const std::size_t batch_entries, ExecutionControl* control) const {
    throw_if_cancelled(stop_token, control);

    std::vector<std::filesystem::path> sources(job.sources.begin(), job.sources.end());
    const auto validation = DestinationCatalog::validate(sources, job.destination);
    if (validation != DestinationValidation::Valid) {
        throw std::filesystem::filesystem_error(
            "Invalid copy destination",
            job.destination,
            std::make_error_code(std::errc::invalid_argument));
    }

    throw_if_cancelled(stop_token, control);

    PathKeyFactory keys;
    std::set<std::wstring, PathKeyLess> source_keys;
    for (const auto& source : job.sources) {
        throw_if_cancelled(stop_token, control);
        const auto key = keys.make(source);
        if (key.empty() || !source_keys.insert(key).second) {
            throw std::filesystem::filesystem_error(
                "Duplicate or invalid copy source",
                source,
                std::make_error_code(std::errc::invalid_argument));
        }
    }

    CopyPlan plan{};
    plan.source_roots = job.sources;
    plan.destination_root = job.destination;
    plan.operation = job.operation;
    const auto flush = [&](bool force = false) {
        const auto entries = plan.files.size() + plan.directories.size() + plan.failures.size();
        if (!sink || !entries || (!force && entries < batch_entries)) return;
        auto batch = std::move(plan);
        plan = {};
        plan.source_roots = job.sources;
        plan.destination_root = job.destination;
        plan.operation = job.operation;
        if (!(*sink)(std::move(batch)))
            throw std::system_error(std::make_error_code(std::errc::operation_canceled));
    };
    std::uint64_t next_file_id = 1;
    OutputRegistry outputs(keys);
    std::set<std::wstring, PathKeyLess> preserved_roots;
    // Top-level names (files and folders) that appear more than once in this
    // job. Only files whose name is in this set are disambiguated by parent.
    std::map<std::wstring, std::size_t, OrdinalNameLess> top_level_names{
        OrdinalNameLess{keys.directory_case_sensitive(job.destination)}};
    for (const auto& source : job.sources) {
        auto name = source.filename().wstring();
        ++top_level_names[name];
    }
    const auto name_collides = [&top_level_names](const std::filesystem::path& source) {
        auto name = source.filename().wstring();
        const auto it = top_level_names.find(name);
        return it != top_level_names.end() && it->second > 1;
    };

    for (const auto& source : job.sources) {
        throw_if_cancelled(stop_token, control);
        flush();

        std::error_code ec;
        const auto status = std::filesystem::symlink_status(source, ec);
        if (ec || !std::filesystem::exists(status)) {
            throw std::filesystem::filesystem_error(
                "Source does not exist",
                source,
                ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory));
        }
        validate_source_reparse_semantics(source);

        const auto root = destination_root_for(source, job.destination, job.layout, status, name_collides(source));
        if (job.layout == DestinationLayout::PreserveSourceFolder) {
            const auto root_key = keys.make(root);
            if (root_key.empty() || !preserved_roots.insert(root_key).second) {
                throw_collision(root);
            }
        }

        if (std::filesystem::is_regular_file(status)) {
            throw_if_cancelled(stop_token, control);
            const auto size = std::filesystem::file_size(source, ec);
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to read file size", source, ec);
            }
            ensure_plan_capacity(plan, source);
            outputs.add_file(root);
            PlannedFile file{next_file_id++, source, root, size, keys.directory_case_sensitive(root.parent_path())};
            account_file(plan, file);
            plan.files.push_back(std::move(file));
            continue;
        }

        if (!std::filesystem::is_directory(status)) {
            throw_unsupported(source);
        }

        ensure_plan_capacity(plan, source);
        outputs.add_directory(root);
        // ContentsOnly maps the folder onto the destination itself, which must
        // never inherit the source folder's Hidden/System state.
        plan.directories.push_back({
            root, job.layout == DestinationLayout::ContentsOnly ? std::filesystem::path{} : source});

        // Walk the tree with an explicit stack so one unreadable folder, a
        // junction/symlink or a file that vanished mid-scan is recorded as a
        // failed item instead of aborting the whole job. Explorer behaves the
        // same way: everything that can be copied is copied and the rest is
        // reported. Job-level problems (cancellation, collisions, the entry
        // limit) still throw.
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> pending_directories;
        pending_directories.emplace_back(source, root);
        while (!pending_directories.empty()) {
            throw_if_cancelled(stop_token, control);
            flush();
            auto [directory_source, directory_target] = std::move(pending_directories.back());
            pending_directories.pop_back();

            // Pin this directory and its ancestors while enumerating, so a
            // previously inspected directory cannot be replaced by a junction.
            detail::DestinationPathGuard source_guard;
            if (!source_guard.lock_existing_chain(directory_source, ec) || !source_guard.missing.empty()) {
                if (!ec) ec = std::error_code(ERROR_PATH_NOT_FOUND, std::system_category());
                if (directory_source == source) {
                    throw std::filesystem::filesystem_error("Unable to enumerate source", source, ec);
                }
                record_failure(plan, directory_source, directory_target, ec);
                ec.clear();
                continue;
            }

            // Enumeration supplies size, attributes and the reparse tag in one
            // query. The engine still checks the opened source before copying.
            WIN32_FIND_DATAW entry{};
            DirectorySearch search;
            search.handle = FindFirstFileExW(
                (directory_source / L"*").c_str(), FindExInfoBasic, &entry,
                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (search.handle == INVALID_HANDLE_VALUE &&
                (GetLastError() == ERROR_NOT_SUPPORTED || GetLastError() == ERROR_INVALID_PARAMETER)) {
                search.handle = FindFirstFileExW(
                    (directory_source / L"*").c_str(), FindExInfoBasic, &entry,
                    FindExSearchNameMatch, nullptr, 0);
            }
            if (search.handle == INVALID_HANDLE_VALUE) {
                const auto error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND) continue; // Empty folder.
                ec = std::error_code(static_cast<int>(error), std::system_category());
                if (directory_source == source) {
                    throw std::filesystem::filesystem_error("Unable to enumerate source", source, ec);
                }
                record_failure(plan, directory_source, directory_target, ec);
                ec.clear();
                continue;
            }

            // Subfolders are visited after this folder's files and in
            // enumeration order (pushed reversed onto the LIFO stack).
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> subdirectories;
            do {
                throw_if_cancelled(stop_token, control);
                flush();
                const std::wstring_view name{entry.cFileName};
                if (name == L"." || name == L"..") continue;
                const auto entry_path = directory_source / name;
                const auto relative = entry_path.lexically_relative(source);
                if (!valid_relative_path(relative)) {
                    throw std::filesystem::filesystem_error(
                        "Unable to resolve relative path",
                        entry_path,
                        source,
                        std::make_error_code(std::errc::invalid_argument));
                }

                const auto target = root / relative;
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                    IsReparseTagNameSurrogate(entry.dwReserved0)) {
                    record_failure(plan, entry_path, target,
                        std::error_code(ERROR_CANT_ACCESS_FILE, std::system_category()));
                    continue;
                }

                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    // Recheck directories before admitting traversal; files
                    // need no per-entry open merely to discover their size.
                    try {
                        validate_source_reparse_semantics(entry_path);
                    } catch (const std::filesystem::filesystem_error& error) {
                        record_failure(plan, entry_path, target, error.code());
                        continue;
                    }
                    ensure_plan_capacity(plan, entry_path);
                    outputs.add_directory(target);
                    plan.directories.push_back({target, entry_path});
                    subdirectories.emplace_back(entry_path, target);
                    continue;
                }

                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) == 0) {
                    const auto size = (static_cast<std::uint64_t>(entry.nFileSizeHigh) << 32) |
                        entry.nFileSizeLow;
                    ensure_plan_capacity(plan, entry_path);
                    outputs.add_file(target);
                    PlannedFile file{next_file_id++, entry_path, target, size, keys.directory_case_sensitive(target.parent_path())};
                    account_file(plan, file);
                    plan.files.push_back(std::move(file));
                    continue;
                }

                record_failure(
                    plan, entry_path, target, std::make_error_code(std::errc::not_supported));
            } while (FindNextFileW(search.handle, &entry));
            const auto enumeration_error = GetLastError();
            pending_directories.insert(
                pending_directories.end(),
                std::make_move_iterator(subdirectories.rbegin()),
                std::make_move_iterator(subdirectories.rend()));
            if (enumeration_error != ERROR_NO_MORE_FILES) {
                // Enumeration stopped part-way through this folder.
                flush(); // Leave room for its failure even at a batch boundary.
                record_failure(plan, directory_source, directory_target,
                    std::error_code(static_cast<int>(enumeration_error), std::system_category()));
            }
        }
    }

    throw_if_cancelled(stop_token, control);
    flush(true);
    return plan;
}

} // namespace velocitycopy
