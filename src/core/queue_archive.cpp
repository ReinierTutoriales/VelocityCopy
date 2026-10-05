#include "velocitycopy/queue_archive.hpp"

#include <windows.h>
#include <objbase.h>
#include <new>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace velocitycopy {
namespace {

constexpr std::array<char, 8> kMagic{'V','C','Q','U','E','U','E','1'};
constexpr std::uint32_t kFormatVersion = 3;
constexpr std::uint32_t kLegacyFormatVersion = 1;
constexpr std::uint32_t kPreviousFormatVersion = 2;
constexpr std::uint32_t kMaxStringChars = 32767;
constexpr std::uint64_t kMaxEntries = 250'000;
constexpr std::uintmax_t kMaxArchiveBytes = 256ull * 1024ull * 1024ull;

template <typename T>
requires std::is_trivially_copyable_v<T>
bool write_value(std::ofstream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

template <typename T>
requires std::is_trivially_copyable_v<T>
bool read_value(std::ifstream& stream, T& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

bool write_wstring(std::ofstream& stream, const std::wstring& value) {
    if (value.size() > kMaxStringChars) return false;
    const auto size = static_cast<std::uint32_t>(value.size());
    if (!write_value(stream, size)) return false;
    if (size == 0) return true;
    stream.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(size * sizeof(wchar_t)));
    return static_cast<bool>(stream);
}

bool read_wstring(std::ifstream& stream, std::wstring& value) {
    std::uint32_t size{};
    if (!read_value(stream, size) || size > kMaxStringChars) return false;
    value.resize(size);
    if (size == 0) return true;
    stream.read(reinterpret_cast<char*>(value.data()),
                static_cast<std::streamsize>(size * sizeof(wchar_t)));
    return static_cast<bool>(stream);
}

bool write_path(std::ofstream& stream, const std::filesystem::path& path) {
    return write_wstring(stream, path.wstring());
}

bool read_path(std::ifstream& stream, std::filesystem::path& path) {
    std::wstring value;
    if (!read_wstring(stream, value)) return false;
    path = std::filesystem::path(std::move(value));
    return true;
}

bool write_plan(std::ofstream& stream, const CopyPlan& plan) {
    const auto operation = static_cast<std::uint8_t>(plan.operation);
    if (!write_value(stream, operation) || !write_path(stream, plan.destination_root)) return false;

    if (plan.source_roots.size() > kMaxEntries ||
        plan.directories.size() > kMaxEntries ||
        plan.files.size() > kMaxEntries) {
        return false;
    }

    const auto roots = static_cast<std::uint64_t>(plan.source_roots.size());
    if (!write_value(stream, roots)) return false;
    for (const auto& root : plan.source_roots) {
        if (!write_path(stream, root)) return false;
    }

    const auto directories = static_cast<std::uint64_t>(plan.directories.size());
    if (!write_value(stream, directories)) return false;
    for (const auto& directory : plan.directories) {
        if (!write_path(stream, directory.destination)) return false;
    }

    const auto files = static_cast<std::uint64_t>(plan.files.size());
    if (!write_value(stream, files)) return false;
    for (const auto& file : plan.files) {
        if (!write_path(stream, file.source) ||
            !write_path(stream, file.destination) ||
            !write_value(stream, file.size)) {
            return false;
        }
    }
    return true;
}

bool read_plan(std::ifstream& stream, CopyPlan& plan, const std::uint32_t version) {
    if (version >= 2) {
        std::uint8_t operation{};
        if (!read_value(stream, operation) ||
            operation > static_cast<std::uint8_t>(FileOperation::Move)) {
            return false;
        }
        plan.operation = static_cast<FileOperation>(operation);
    } else {
        plan.operation = FileOperation::Copy;
    }
    if (!read_path(stream, plan.destination_root)) return false;

    std::uint64_t roots{};
    if (!read_value(stream, roots) || roots > kMaxEntries) return false;
    for (std::uint64_t i = 0; i < roots; ++i) {
        std::filesystem::path path;
        if (!read_path(stream, path)) return false;
        plan.source_roots.push_back(std::move(path));
    }

    std::uint64_t directories{};
    if (!read_value(stream, directories) || directories > kMaxEntries) return false;
    for (std::uint64_t i = 0; i < directories; ++i) {
        std::filesystem::path path;
        if (!read_path(stream, path)) return false;
        plan.directories.push_back({std::move(path)});
    }

    std::uint64_t files{};
    if (!read_value(stream, files) || files > kMaxEntries) return false;
    std::uint64_t next_id = 1;
    for (std::uint64_t i = 0; i < files; ++i) {
        PlannedFile file{};
        file.id = next_id++;
        if (!read_path(stream, file.source) ||
            !read_path(stream, file.destination) ||
            !read_value(stream, file.size)) {
            return false;
        }
        if (std::numeric_limits<std::uint64_t>::max() - plan.total_bytes < file.size) {
            return false;
        }
        plan.total_bytes += file.size;
        plan.largest_file_bytes = (std::max)(plan.largest_file_bytes, file.size);
        plan.files.push_back(std::move(file));
    }
    return true;
}

bool write_job(std::ofstream& stream, const CopyJob& job) {
    if (job.sources.size() > kMaxEntries) return false;
    if (!write_path(stream, job.destination)) return false;
    const auto layout = static_cast<std::uint8_t>(job.layout);
    const auto operation = static_cast<std::uint8_t>(job.operation);
    if (!write_value(stream, layout) || !write_value(stream, operation) ||
        !write_wstring(stream, job.display_name)) return false;

    const auto sources = static_cast<std::uint64_t>(job.sources.size());
    if (!write_value(stream, sources)) return false;
    for (const auto& source : job.sources) {
        if (!write_path(stream, source)) return false;
    }
    return true;
}

bool read_job(std::ifstream& stream, CopyJob& job, const std::uint32_t version) {
    if (!read_path(stream, job.destination)) return false;
    std::uint8_t layout{};
    if (!read_value(stream, layout) ||
        layout > static_cast<std::uint8_t>(DestinationLayout::ContentsOnly)) {
        return false;
    }
    job.layout = static_cast<DestinationLayout>(layout);
    if (version >= 2) {
        std::uint8_t operation{};
        if (!read_value(stream, operation) ||
            operation > static_cast<std::uint8_t>(FileOperation::Move)) {
            return false;
        }
        job.operation = static_cast<FileOperation>(operation);
    } else {
        job.operation = FileOperation::Copy;
    }
    if (!read_wstring(stream, job.display_name)) return false;

    std::uint64_t sources{};
    if (!read_value(stream, sources) || sources > kMaxEntries) return false;
    for (std::uint64_t i = 0; i < sources; ++i) {
        std::filesystem::path path;
        if (!read_path(stream, path)) return false;
        job.sources.push_back(std::move(path));
    }
    job.state = JobState::Pending;
    return true;
}

bool write_jobs(std::ofstream& stream, const std::vector<CopyJob>& jobs) {
    if (jobs.size() > kMaxEntries) return false;
    const auto count = static_cast<std::uint64_t>(jobs.size());
    if (!write_value(stream, count)) return false;
    for (const auto& job : jobs) {
        if (!write_job(stream, job)) return false;
    }
    return true;
}

bool read_jobs(std::ifstream& stream, std::vector<CopyJob>& jobs, const std::uint32_t version) {
    std::uint64_t count{};
    if (!read_value(stream, count) || count > kMaxEntries) return false;
    for (std::uint64_t i = 0; i < count; ++i) {
        CopyJob job{};
        if (!read_job(stream, job, version)) return false;
        jobs.push_back(std::move(job));
    }
    return true;
}

bool write_fingerprint(std::ofstream& stream, const FileFingerprint& value) {
    const std::uint8_t has_identity = value.has_identity ? 1 : 0;
    if (!write_value(stream, value.size) ||
        !write_value(stream, value.last_write_time) ||
        !write_value(stream, has_identity)) return false;
    if (!value.has_identity) return true;
    if (!write_value(stream, value.identity.volume_serial)) return false;
    stream.write(
        reinterpret_cast<const char*>(value.identity.file_id.data()),
        static_cast<std::streamsize>(value.identity.file_id.size()));
    return static_cast<bool>(stream);
}

bool read_fingerprint(std::ifstream& stream, FileFingerprint& value) {
    std::uint8_t has_identity{};
    if (!read_value(stream, value.size) ||
        !read_value(stream, value.last_write_time) ||
        !read_value(stream, has_identity) || has_identity > 1) return false;
    value.has_identity = has_identity != 0;
    if (!value.has_identity) return true;
    if (!read_value(stream, value.identity.volume_serial)) return false;
    stream.read(
        reinterpret_cast<char*>(value.identity.file_id.data()),
        static_cast<std::streamsize>(value.identity.file_id.size()));
    return static_cast<bool>(stream);
}

bool write_source_removals(
    std::ofstream& stream,
    const std::vector<SourceRemovalRecovery>& recoveries) {
    if (recoveries.size() > kMaxEntries) return false;
    const auto count = static_cast<std::uint64_t>(recoveries.size());
    if (!write_value(stream, count)) return false;
    for (const auto& recovery : recoveries) {
        if (!write_value(stream, recovery.hresult) ||
            !write_path(stream, recovery.source) ||
            !write_path(stream, recovery.destination)) return false;
        const std::uint8_t destination_preexisted = recovery.destination_preexisted ? 1 : 0;
        if (!write_value(stream, destination_preexisted) ||
            !write_value(stream, recovery.attempt_count) ||
            !write_fingerprint(stream, recovery.source_fingerprint) ||
            !write_fingerprint(stream, recovery.destination_fingerprint)) return false;
    }
    return true;
}

bool read_source_removals(
    std::ifstream& stream,
    std::vector<SourceRemovalRecovery>& recoveries) {
    std::uint64_t count{};
    if (!read_value(stream, count) || count > kMaxEntries) return false;
    recoveries.reserve(static_cast<std::size_t>((std::min<std::uint64_t>)(count, 4096)));
    for (std::uint64_t index = 0; index < count; ++index) {
        SourceRemovalRecovery recovery{};
        std::uint8_t destination_preexisted{};
        if (!read_value(stream, recovery.hresult) ||
            !read_path(stream, recovery.source) ||
            !read_path(stream, recovery.destination) ||
            !read_value(stream, destination_preexisted) || destination_preexisted > 1 ||
            !read_value(stream, recovery.attempt_count) || recovery.attempt_count == 0 ||
            !read_fingerprint(stream, recovery.source_fingerprint) ||
            !read_fingerprint(stream, recovery.destination_fingerprint)) return false;
        recovery.destination_preexisted = destination_preexisted != 0;
        recoveries.push_back(std::move(recovery));
    }
    return true;
}

bool flush_file_to_disk(const std::filesystem::path& path) noexcept {
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    return ok;
}

std::filesystem::path temp_path_for(const std::filesystem::path& path) {
    auto temp = path;
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::bad_alloc{};
    wchar_t text[39]{};
    if (StringFromGUID2(id, text, 39) == 0) throw std::bad_alloc{};
    temp += L".";
    temp += text;
    temp += L".tmp";
    return temp;
}

class TempFileGuard final {
public:
    explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
    ~TempFileGuard() {
        if (!committed_) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }
    void commit() noexcept { committed_ = true; }

private:
    std::filesystem::path path_;
    bool committed_{};
};

} // namespace

bool QueueArchiveStore::save(
    const std::filesystem::path& path,
    const QueueArchive& archive) const noexcept {
    try {
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) return false;
        }

        const auto temp = temp_path_for(path);
        TempFileGuard temp_guard(temp);
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) return false;

        stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
        const std::uint8_t has_current = archive.current_plan ? 1 : 0;
        if (!stream || !write_value(stream, kFormatVersion) ||
            !write_value(stream, has_current)) {
            return false;
        }
        if (archive.current_plan && !write_plan(stream, *archive.current_plan)) return false;
        if (!write_jobs(stream, archive.current_append_jobs) ||
            !write_jobs(stream, archive.queued_jobs) ||
            !write_source_removals(stream, archive.source_removals)) {
            return false;
        }

        stream.flush();
        if (!stream) return false;
        stream.close();
        if (!flush_file_to_disk(temp)) return false;

        const auto flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
        if (!MoveFileExW(temp.c_str(), path.c_str(), flags)) {
            return false;
        }
        temp_guard.commit();
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<QueueArchive> QueueArchiveStore::load(
    const std::filesystem::path& path) const noexcept {
    try {
        std::error_code size_error;
        const auto archive_size = std::filesystem::file_size(path, size_error);
        if (size_error || archive_size > kMaxArchiveBytes) {
            return std::nullopt;
        }

        std::ifstream stream(path, std::ios::binary);
        if (!stream) return std::nullopt;

        std::array<char, kMagic.size()> magic{};
        stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        std::uint32_t version{};
        std::uint8_t has_current{};
        if (!stream || magic != kMagic || !read_value(stream, version) ||
            (version != kFormatVersion && version != kPreviousFormatVersion &&
             version != kLegacyFormatVersion) ||
            !read_value(stream, has_current) || has_current > 1) {
            return std::nullopt;
        }

        QueueArchive archive{};
        if (has_current != 0) {
            CopyPlan plan{};
            if (!read_plan(stream, plan, version)) return std::nullopt;
            archive.current_plan = std::move(plan);
        }

        if (!read_jobs(stream, archive.current_append_jobs, version) ||
            !read_jobs(stream, archive.queued_jobs, version)) {
            return std::nullopt;
        }
        if (version >= 3 && !read_source_removals(stream, archive.source_removals)) {
            return std::nullopt;
        }

        if (stream.peek() != std::ifstream::traits_type::eof()) {
            return std::nullopt;
        }
        return archive;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace velocitycopy
