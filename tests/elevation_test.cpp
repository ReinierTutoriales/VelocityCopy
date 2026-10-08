#include "velocitycopy/elevation.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace velocitycopy;

int fail(const int code, const char* message) {
    std::cerr << "elevation_test " << code << ": " << message << '\n';
    return code;
}
} // namespace

int wmain() {
    const auto root = fs::temp_directory_path() / L"VelocityCopyElevationTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"destination");

    // A writable destination never asks for administrator rights, whether the
    // root exists (files land in it) or has to be created.
    CopyPlan plan{};
    plan.destination_root = root / L"destination";
    plan.files.push_back({1, root / L"source.txt", root / L"destination" / L"source.txt", 1});
    if (destination_requires_elevation(plan)) return fail(1, "writable destination reported as protected");
    plan.destination_root = root / L"missing" / L"nested";
    plan.files.front().destination = plan.destination_root / L"source.txt";
    if (destination_requires_elevation(plan)) return fail(2, "creatable destination reported as protected");
    if (destination_requires_elevation(root / L"destination")) return fail(3, "writable folder reported as protected");
    for (const auto& entry : fs::directory_iterator(root / L"destination")) {
        (void)entry;
        return fail(4, "the write probe left an entry behind");
    }

    QueueArchive archive{};
    CopyJob job{};
    job.sources.push_back(root / L"source.txt");
    job.destination = root / L"destination";
    archive.queued_jobs.push_back(job);

    // Round trip: the elevated side gets exactly the job and the file is gone.
    {
        const auto handoff = write_elevated_handoff(root, archive);
        if (!handoff || handoff->sha256.size() != 64 || !fs::exists(handoff->file)) {
            return fail(5, "handoff was not written");
        }
        const auto loaded = take_elevated_handoff(handoff->file, handoff->sha256);
        if (!loaded || loaded->queued_jobs.size() != 1 ||
            loaded->queued_jobs.front().destination != job.destination ||
            loaded->queued_jobs.front().sources != job.sources) {
            return fail(6, "handoff did not round-trip");
        }
        if (fs::exists(handoff->file)) return fail(7, "consumed handoff was not deleted");
    }

    // A file changed after it was written is refused (and still removed).
    {
        const auto handoff = write_elevated_handoff(root, archive);
        if (!handoff) return fail(8, "handoff was not written");
        {
            std::ofstream stream(handoff->file, std::ios::binary | std::ios::app);
            stream << 'x';
        }
        if (take_elevated_handoff(handoff->file, handoff->sha256)) return fail(9, "tampered handoff accepted");
        if (fs::exists(handoff->file)) return fail(10, "refused handoff was not deleted");
    }

    // Only VelocityCopy handoff files are ever read or deleted.
    {
        const auto other = root / L"notes.vcq";
        std::ofstream(other, std::ios::binary) << "keep";
        if (take_elevated_handoff(other, std::wstring(64, L'0'))) return fail(11, "foreign file accepted");
        if (!fs::exists(other)) return fail(12, "foreign file was deleted");
    }

    fs::remove_all(root, ec);
    return 0;
}
