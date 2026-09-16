#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/destination_catalog.hpp"

#include <algorithm>
#include <limits>
#include <system_error>

namespace velocitycopy {
namespace {

std::filesystem::path destination_root_for(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    DestinationLayout layout,
    const std::filesystem::file_status& status) {
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

    if (is_regular_file) {
        const auto immediate_parent = source.parent_path().filename();
        if (!immediate_parent.empty()) {
            return destination / immediate_parent / source.filename();
        }
        return destination / source.filename();
    }

    return destination / source.filename();
}

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

} // namespace

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
    return true;
}

CopyPlan JobPlanner::build(const CopyJob& job) const {
    std::vector<std::filesystem::path> sources(job.sources.begin(), job.sources.end());
    const auto validation = DestinationCatalog::validate(sources, job.destination);
    if (validation != DestinationValidation::Valid) {
        throw std::filesystem::filesystem_error(
            "Invalid copy destination",
            job.destination,
            std::make_error_code(std::errc::invalid_argument));
    }

    CopyPlan plan{};
    plan.source_roots = job.sources;
    plan.destination_root = job.destination;
    std::uint64_t next_file_id = 1;

    for (const auto& source : job.sources) {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(source, ec);
        if (ec || !std::filesystem::exists(status)) {
            throw std::filesystem::filesystem_error("Source does not exist", source, ec);
        }
        if (std::filesystem::is_symlink(status)) {
            throw_unsupported(source);
        }

        const auto root = destination_root_for(source, job.destination, job.layout, status);

        if (std::filesystem::is_regular_file(status)) {
            const auto size = std::filesystem::file_size(source, ec);
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to read file size", source, ec);
            }
            plan.files.push_back({next_file_id++, source, root, size});
            checked_add(plan.total_bytes, size, source);
            continue;
        }

        if (!std::filesystem::is_directory(status)) {
            throw_unsupported(source);
        }

        plan.directories.push_back({root});

        std::filesystem::recursive_directory_iterator it(source, std::filesystem::directory_options::none, ec);
        const std::filesystem::recursive_directory_iterator end;
        if (ec) {
            throw std::filesystem::filesystem_error("Unable to enumerate source", source, ec);
        }

        for (; it != end; it.increment(ec)) {
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to enumerate source", source, ec);
            }

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
                throw std::filesystem::filesystem_error("Unable to inspect source", entry.path(), ec);
            }

            if (std::filesystem::is_symlink(entry_status)) {
                throw_unsupported(entry.path());
            }

            if (std::filesystem::is_directory(entry_status)) {
                plan.directories.push_back({target});
                continue;
            }

            if (std::filesystem::is_regular_file(entry_status)) {
                const auto size = entry.file_size(ec);
                if (ec) {
                    throw std::filesystem::filesystem_error("Unable to read file size", entry.path(), ec);
                }
                plan.files.push_back({next_file_id++, entry.path(), target, size});
                checked_add(plan.total_bytes, size, entry.path());
                continue;
            }

            throw_unsupported(entry.path());
        }
    }

    return plan;
}

} // namespace velocitycopy
