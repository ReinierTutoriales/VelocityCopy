#pragma once
#include "velocitycopy/copy_job.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace velocitycopy {
enum class RouteDecision { StartNew, AppendTo, WaitFor, Ask };
enum class RouteChoice { Append, Wait, Parallel };
struct StorageKey { std::wstring volume; std::optional<std::uint32_t> disk; };
[[nodiscard]] StorageKey resolve_storage_key(const std::filesystem::path&) noexcept;
[[nodiscard]] std::wstring fallback_volume_key(const std::filesystem::path&) noexcept;
struct ActiveSession { std::uint64_t window_id{}; std::filesystem::path destination_root; FileOperation operation{FileOperation::Copy}; bool accepting_appends{}; StorageKey destination, source; };
struct TransferRequest { std::filesystem::path destination_root; FileOperation operation{FileOperation::Copy}; StorageKey destination, source; };
struct RoutePreferences { std::optional<RouteChoice> same_destination; std::optional<RouteChoice> same_device; };
struct RouteResult { RouteDecision decision{RouteDecision::StartNew}; std::uint64_t window_id{}; std::vector<RouteChoice> offered; RouteChoice recommended{RouteChoice::Parallel}; };
[[nodiscard]] bool same_destination(const std::filesystem::path&, const std::filesystem::path&) noexcept;
[[nodiscard]] bool same_device(const StorageKey&, const StorageKey&) noexcept;
[[nodiscard]] RouteResult route_transfer(const TransferRequest&, std::span<const ActiveSession>, const RoutePreferences& = {});
}
