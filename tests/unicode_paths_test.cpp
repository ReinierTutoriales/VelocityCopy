#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/path_key.hpp"
#include "velocitycopy/transfer_router.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace velocitycopy;

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if (!stream) throw std::runtime_error("Unable to create Unicode fixture");
}
std::string read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
CopyPlan plan_with_file(const fs::path& root, const fs::path& target, const std::uint64_t id = 1) {
    CopyPlan plan{};
    plan.destination_root = root;
    plan.source_roots = {root.parent_path() / L"source"};
    plan.files = {{id, plan.source_roots.front() / L"file", target, 1}};
    plan.total_bytes = 1;
    return plan;
}

int run_test() {
    const auto root = fs::temp_directory_path() /
        (L"VelocityCopyUnicodePathsTest-" + std::to_wstring(GetCurrentProcessId()));
    const auto destination = root / L"destination";
    fs::create_directories(destination);
    PathKeyFactory keys;
    if (!path_key_equal(keys.make(destination / L"É.txt"), keys.make(destination / L"é.txt"))) return 1;
    if (!path_key_equal(keys.make(destination / L"I.txt"), keys.make(destination / L"i.txt"))) return 2;
    if (path_key_equal(keys.make(destination / L"é.txt"), keys.make(destination / L"é.txt"))) return 3;
    if (path_key_equal(keys.make(destination / L"İ.txt"), keys.make(destination / L"i.txt"))) return 4;
    if (!same_destination(destination / L"É", destination / L"é")) return 5;
    if (DestinationCatalog::validate(std::vector<fs::path>{destination / L"É"}, destination / L"é") !=
        DestinationValidation::SameAsSource) return 6;
    if (DestinationCatalog::validate(std::vector<fs::path>{destination / L"É"}, destination / L"é" / L"child") !=
        DestinationValidation::InsideSource) return 7;
    if (DestinationCatalog::validate(std::vector<fs::path>{destination / L"É"}, destination / L"Éx") !=
        DestinationValidation::Valid) return 8;

    // Loose Unicode names that collide in the destination must be disambiguated
    // by source parent, just like the existing ASCII collision policy.
    const auto upper = root / L"one" / L"É.txt";
    const auto lower = root / L"two" / L"é.txt";
    write_text(upper, "UPPER");
    write_text(lower, "lower");
    CopyJob job{};
    job.sources = {upper, lower};
    job.destination = destination;
    const auto raw = JobPlanner{}.build(job);
    if (raw.files.size() != 2 || raw.files[0].destination != destination / L"one" / L"É.txt" ||
        raw.files[1].destination != destination / L"two" / L"é.txt") return 9;
    if (!JobExecutor{}.execute(raw).success || read_text(destination / L"one" / L"é.txt") != "UPPER" ||
        read_text(destination / L"two" / L"É.txt") != "lower") return 10;

    LiveCopyPlan live(plan_with_file(destination, destination / L"É.txt"));
    if (live.append(plan_with_file(destination, destination / L"é.txt")) !=
        LivePlanAppendResult::DestinationCollision) return 11;
    auto duplicate = plan_with_file(destination, destination / L"É.txt");
    duplicate.files.push_back({2, lower, destination / L"é.txt", 1});
    bool rejected = false;
    try { LiveCopyPlan invalid(std::move(duplicate)); }
    catch (const fs::filesystem_error&) { rejected = true; }
    if (!rejected) return 12;

    auto keep_both = plan_with_file(destination, destination / L"É.txt");
    keep_both.files.push_back({2, lower, destination / L"é (2).txt", 1});
    LiveCopyPlan alternatives(std::move(keep_both));
    const auto active = alternatives.acquire_next();
    if (!active) return 13;
    const auto redirected = alternatives.redirect_active_destination(active->id);
    if (!redirected || redirected->filename() != L"É (3).txt") return 14;

    auto under = plan_with_file(destination, destination / L"é" / L"file.txt");
    under.files.push_back({2, upper, destination / L"Éx" / L"file.txt", 1});
    LiveCopyPlan prefixes(std::move(under));
    if (prefixes.fail_pending_under(destination / L"É", HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) != 1 ||
        prefixes.remaining_files() != 1) return 15;

    // Explicit long-path spelling and ordinary spelling share comparison keys.
    auto long_parent = root / L"long";
    for (int part = 0; part < 6; ++part) long_parent /= std::wstring(55, L'x') + std::to_wstring(part);
    const fs::path extended = L"\\\\?\\" + long_parent.native();
    fs::create_directories(extended);
    if (!path_key_equal(keys.make(extended / L"É.txt"), keys.make(long_parent / L"é.txt"))) return 16;
    job.sources = {upper};
    job.destination = extended;
    job.layout = DestinationLayout::ContentsOnly;
    LiveCopyPlan long_plan(JobPlanner{}.build(job));
    const auto long_copy = JobExecutor{}.execute(long_plan);
    if (!long_copy.success || long_copy.outcomes.succeeded != 1 || read_text(extended / L"É.txt") != "UPPER") {
        std::cerr << "Long-path native status: " << std::hex << long_copy.native_code << '\n';
        for (const auto& incident : long_plan.parked_incidents())
            std::cerr << "Long-path incident: " << std::hex << incident.hresult << '\n';
        return 17;
    }
    fs::remove_all(fs::path(L"\\\\?\\" + root.native()));
    return 0;
}

int main() {
    try {
        const auto result = run_test();
        if (result) std::cerr << "Unicode paths failed at check " << result << '\n';
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 99;
    }
}
