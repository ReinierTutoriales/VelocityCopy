#pragma once

#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/queue_archive.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace velocitycopy {

// True when this process runs with an elevated (administrator) token.
[[nodiscard]] bool process_is_elevated() noexcept;

// True when writing `plan` needs administrator rights: this process is not
// elevated and the folder's security denies creating what the plan writes
// first (a file or a folder) in the nearest existing destination folder. The
// drive root is the usual case: standard users may create folders there but
// not files.
// Nothing is written by the check. Any other outcome returns false; the
// engine still reports real failures per item.
[[nodiscard]] bool destination_requires_elevation(const CopyPlan& plan) noexcept;

// Same check for an arbitrary destination folder, for creating a file in it.
[[nodiscard]] bool destination_requires_elevation(const std::filesystem::path& destination) noexcept;

struct ElevatedHandoff {
    std::filesystem::path file;
    std::wstring sha256;
};

// Writes `archive` to a new file in `directory` and returns its SHA-256, which
// travels on the elevated command line so the elevated process only runs the
// exact work this process wrote.
[[nodiscard]] std::optional<ElevatedHandoff> write_elevated_handoff(
    const std::filesystem::path& directory,
    const QueueArchive& archive) noexcept;

// Starts `executable --elevated-handoff <file> <sha256>` through UAC. Returns
// S_OK when the elevated process started, HRESULT_FROM_WIN32(ERROR_CANCELLED)
// when the person declined the prompt, or the failure. The handoff file is
// deleted unless the elevated process started.
[[nodiscard]] std::int32_t launch_elevated_handoff(
    const std::filesystem::path& executable,
    const ElevatedHandoff& handoff,
    void* owner_window) noexcept;

// Elevated side: loads the handoff only when its content still matches
// `expected_sha256` (the file is held open without write/delete sharing from
// hashing until parsing) and deletes it afterwards.
[[nodiscard]] std::optional<QueueArchive> take_elevated_handoff(
    const std::filesystem::path& file,
    std::wstring_view expected_sha256) noexcept;

} // namespace velocitycopy
