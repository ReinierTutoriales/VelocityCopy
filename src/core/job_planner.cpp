#include "velocitycopy/job_planner.hpp"

#include <system_error>

namespace velocitycopy {
namespace {

std::filesystem::path destination_root_for(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    DestinationLayout layout) {
    if (layout == DestinationLayout::ContentsOnly && std::filesystem::is_directory(source)) {
        return destination;
    }

    return destination / source.filename();
}

} // namespace

CopyPlan JobPlanner::build(const CopyJob& job) const {
    CopyPlan plan{};

    for (const auto& source : job.sources) {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(source, ec);
        if (ec || !std::filesystem::exists(status)) {
            throw std::filesystem::filesystem_error("Source does not exist", source, ec);
        }

        const auto root = destination_root_for(source, job.destination, job.layout);

        if (std::filesystem::is_regular_file(status)) {
            const auto size = std::filesystem::file_size(source, ec);
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to read file size", source, ec);
            }
            plan.files.push_back({source, root, size});
            plan.total_bytes += size;
            continue;
        }

        if (!std::filesystem::is_directory(status)) {
            continue;
        }

        plan.directories.push_back({root});

        std::filesystem::recursive_directory_iterator it(
            source,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        const std::filesystem::recursive_directory_iterator end;

        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            const auto& entry = *it;
            const auto relative = std::filesystem::relative(entry.path(), source, ec);
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to resolve relative path", entry.path(), source, ec);
            }

            const auto target = root / relative;
            const auto entry_status = entry.symlink_status(ec);
            if (ec) {
                throw std::filesystem::filesystem_error("Unable to inspect source", entry.path(), ec);
            }

            if (std::filesystem::is_symlink(entry_status)) {
                it.disable_recursion_pending();
                continue;
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
                plan.files.push_back({entry.path(), target, size});
                plan.total_bytes += size;
            }
        }
    }

    return plan;
}

} // namespace velocitycopy
