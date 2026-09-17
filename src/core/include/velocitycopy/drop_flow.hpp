#pragma once

#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/drop_menu_model.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace velocitycopy {

enum class DropFlowStage : std::uint8_t {
    Destination,
    Layout,
    Ready,
};

class DropFlowController final {
public:
    void begin(std::span<const DropItem> items);
    void reset() noexcept;

    [[nodiscard]] DropFlowStage stage() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::span<const DropItem> items() const noexcept;
    [[nodiscard]] const std::filesystem::path& destination() const noexcept;
    [[nodiscard]] const std::optional<DropMenuModel>& menu() const noexcept;
    [[nodiscard]] const std::optional<DestinationLayout>& selected_layout() const noexcept;

    [[nodiscard]] DestinationValidation choose_destination(
        const std::filesystem::path& destination);
    [[nodiscard]] bool choose_layout(DestinationLayout layout) noexcept;
    [[nodiscard]] bool back() noexcept;

    [[nodiscard]] std::optional<CopyJob> make_job(
        std::uint64_t job_id = 0,
        FileOperation operation = FileOperation::Copy) const;

private:
    std::vector<DropItem> items_;
    std::filesystem::path destination_;
    std::optional<DropMenuModel> menu_;
    std::optional<DestinationLayout> selected_layout_;
    DropFlowStage stage_{DropFlowStage::Destination};
    DropMenuBuilder builder_;
};

} // namespace velocitycopy
