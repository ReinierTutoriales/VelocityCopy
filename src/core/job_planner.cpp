#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/destination_catalog.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>
#include <stop_token>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace velocitycopy {
namespace {

constexpr std::size_t kMaxPlannedEntries = 250'000;

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

std::wstring normalized_path_key(const std::filesystem::path& input) {
    if (input.empty()) {
        return {};
    }

    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetFullPathNameW(
        input.c_str(),
        static_cast<DWORD>(buffer.size()),
        buffer.data(),
        nullptr);
    if (length == 0 || length >= buffer.size()) {
        return {};
    }

    std::wstring result(buffer.data(), length);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) {
        result.pop_back();
    }
    std::transform(result.begin(), result.end(), result.begin(), [](const wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return result;
}

[[noreturn]] void throw_collision(const std::filesystem::path& path) {
    throw std::filesystem::filesystem_error(
        "Multiple copy entries resolve to the same destination",
        path,
        std::make_error_code(std::errc::file_exists));
}

void throw_if_cancelled(const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
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
    void add_directory(const std::filesystem::path& path) {
        auto cursor = path;
        while (!cursor.empty()) {
            const auto key = normalized_path_key(cursor);
            if (key.empty()) {
                throw std::filesystem::filesystem_error(
                    "Unable to normalize destination path",
                    cursor,
                    std::make_error_code(std::errc::invalid_argument));
            }
            if (files_.contains(key)) {
                throw_collision(cursor);
            }
            directories_.insert(key);

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

        const auto key = normalized_path_key(path);
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
    std::unordered_set<std::wstring> files_;
    std::unordered_set<std::wstring> directories_;
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

bool CopyPlan::move_file(const std::uint64_t file_id, const std::size_t new_index) noexcept {
    if (new_index >= files.size()) {
        return false;
    }

    const auto it = std::find_if(files.begin(), files.end(), [file_id](const PlannedFile& file) {
        return file.id == file_id;
    });
    if (it == files.end()) {
        return false;
    }

    const auto current_index = static_cast<std::size_t>(std::distance(files.begin(), it));
    if (current_index == new_index) {
        return true;
    }

    if (current_index < new_index) {
        std::rotate(it, it + 1, files.begin() + static_cast<std::ptrdiff_t>(new_index + 1));
    } else {
        std::rotate(files.begin() + static_cast<std::ptrdiff_t>(new_index), it, it + 1);
    }

    return true;
}

bool CopyPlan::move_file_up(const std::uint64_t file_id) noexcept {
    const auto it = std::find_if(files.begin(), files.end(), [file_id](const PlannedFile& file) {
        return file.id == file_id;
    });
    if (it == files.end() || it == files.begin()) {
        return false;
    }

    const auto index = static_cast<std::size_t>(std::distance(files.begin(), it));
    return move_file(file_id, index - 1);
}

bool CopyPlan::move_file_down(const std::uint64_t file_id) noexcept {
    const auto it = std::find_if(files.begin(), files.end(), [file_id](const PlannedFile& file) {
        return file.id == file_id;
    });
    if (it == files.end()) {
        return false;
    }

    const auto index = static_cast<std::size_t>(std::distance(files.begin(), it));
    if (index + 1 >= files.size()) {
        return false;
    }

    return move_file(file_id, index + 1);
}

bool CopyPlan::remove_file(const std::uint64_t file_id) noexcept {
    const auto it = std::find_if(files.begin(), files.end(), [file_id](const PlannedFile& file) {
        return file.id == file_id;
    });
    if (it == files.end()) {
        return false;
    }

    total_bytes -= it->size;
    files.erase(it);
    largest_file_bytes = 0;
    for (const auto& file : files) {
        largest_file_bytes = std::max(largest_file_bytes, file.size);
    }
    return true;
}

CopyPlan JobPlanner::build(const CopyJob& job) const {
    return build(job, {});
}

CopyPlan JobPlanner::build(const CopyJob& job, const std::stop_token stop_token) const {
    throw_if_cancelled(stop_token);

    std::vector<std::filesystem::path> sources(job.sources.begin(), job.sources.end());
    const auto validation = DestinationCatalog::validate(sources, job.destination);
    if (validation != DestinationValidation::Valid) {
        throw std::filesystem::filesystem_error(
            "Invalid copy destination",
            job.destination,
            std::make_error_code(std::errc::invalid_argument));
    }

    throw_if_cancelled(stop_token);

    std::unordered_set<std::wstring> source_keys;
    source_keys.reserve(job.sources.size());
    for (const auto& source : job.sources) {
        throw_if_cancelled(stop_token);
        const auto key = normalized_path_key(source);
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
    std::uint64_t next_file_id = 1;
    OutputRegistry outputs;
    std::unordered_set<std::wstring> preserved_roots;
    // Top-level names (files and folders) that appear more than once in this
    // job. Only files whose name is in this set are disambiguated by parent.
    std::unordered_map<std::wstring, std::size_t> top_level_names;
    top_level_names.reserve(job.sources.size());
    for (const auto& source : job.sources) {
        auto name = source.filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(), [](const wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
        ++top_level_names[name];
    }
    const auto name_collides = [&top_level_names](const std::filesystem::path& source) {
        auto name = source.filename().wstring();
        std::transform(name.begin(), name.end(), name.begin(), [](const wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
        const auto it = top_level_names.find(name);
        return it != top_level_names.end() && it->second > 1;
    };

    for (const auto& source : job.sources) {
        throw_if_cancelled(stop_token);

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
            const auto root_key = normalized_path_key(root);
            if (root_key.empty() || !preserved_roots.insert(root_key).second) {
                throw_collision(root);
            }
        }

        if (std::filesystem::is_regular_file(status)) {
            throw_if_cancelled(stop_token);
            const auto size = std::filesystem::file_size(source, ec);
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to read file size", source, ec);
            }
            ensure_plan_capacity(plan, source);
            outputs.add_file(root);
            PlannedFile file{next_file_id++, source, root, size};
            account_file(plan, file);
            plan.files.push_back(std::move(file));
            continue;
        }

        if (!std::filesystem::is_directory(status)) {
            throw_unsupported(source);
        }

        ensure_plan_capacity(plan, source);
        outputs.add_directory(root);
        plan.directories.push_back({root});

        // Walk the tree with an explicit stack so one unreadable folder, a
        // junction/symlink or a file that vanished mid-scan is recorded as a
        // failed item instead of aborting the whole job. Explorer behaves the
        // same way: everything that can be copied is copied and the rest is
        // reported. Job-level problems (cancellation, collisions, the entry
        // limit) still throw.
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> pending_directories;
        pending_directories.emplace_back(source, root);
        while (!pending_directories.empty()) {
            throw_if_cancelled(stop_token);
            auto [directory_source, directory_target] = std::move(pending_directories.back());
            pending_directories.pop_back();

            std::filesystem::directory_iterator it(directory_source, std::filesystem::directory_options::none, ec);
            if (ec) {
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
            for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
                throw_if_cancelled(stop_token);
                if (ec) break;

                const auto& entry = *it;
                const auto relative = entry.path().lexically_relative(source);
                if (!valid_relative_path(relative)) {
                    throw std::filesystem::filesystem_error(
                        "Unable to resolve relative path",
                        entry.path(),
                        source,
                        std::make_error_code(std::errc::invalid_argument));
                }

                const auto target = root / relative;
                const auto entry_status = entry.symlink_status(ec);
                if (ec) {
                    record_failure(plan, entry.path(), target, ec);
                    ec.clear();
                    continue;
                }

                try {
                    validate_source_reparse_semantics(entry.path());
                } catch (const std::filesystem::filesystem_error& error) {
                    // Junctions and symbolic links are never traversed (they
                    // could lead outside the selected tree); report them.
                    record_failure(plan, entry.path(), target, error.code());
                    continue;
                }

                if (std::filesystem::is_directory(entry_status)) {
                    ensure_plan_capacity(plan, entry.path());
                    outputs.add_directory(target);
                    plan.directories.push_back({target});
                    subdirectories.emplace_back(entry.path(), target);
                    continue;
                }

                if (std::filesystem::is_regular_file(entry_status)) {
                    const auto size = entry.file_size(ec);
                    if (ec) {
                        record_failure(plan, entry.path(), target, ec);
                        ec.clear();
                        continue;
                    }
                    ensure_plan_capacity(plan, entry.path());
                    outputs.add_file(target);
                    PlannedFile file{next_file_id++, entry.path(), target, size};
                    account_file(plan, file);
                    plan.files.push_back(std::move(file));
                    continue;
                }

                record_failure(
                    plan, entry.path(), target, std::make_error_code(std::errc::not_supported));
            }
            pending_directories.insert(
                pending_directories.end(),
                std::make_move_iterator(subdirectories.rbegin()),
                std::make_move_iterator(subdirectories.rend()));
            if (ec) {
                // Enumeration stopped part-way through this folder.
                record_failure(plan, directory_source, directory_target, ec);
                ec.clear();
            }
        }
    }

    throw_if_cancelled(stop_token);
    return plan;
}

} // namespace velocitycopy
