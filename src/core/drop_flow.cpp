#include "velocitycopy/drop_flow.hpp"

#include <utility>

namespace velocitycopy {

void DropFlowController::begin(std::span<const DropItem> items) {
    items_.assign(items.begin(), items.end());
    destination_.clear();
    menu_.reset();
    selected_layout_.reset();
    stage_ = DropFlowStage::Destination;
}

void DropFlowController::reset() noexcept {
    items_.clear();
    destination_.clear();
    menu_.reset();
    selected_layout_.reset();
    stage_ = DropFlowStage::Destination;
}

DropFlowStage DropFlowController::stage() const noexcept {
    return stage_;
}

bool DropFlowController::empty() const noexcept {
    return items_.empty();
}

std::span<const DropItem> DropFlowController::items() const noexcept {
    return items_;
}

const std::filesystem::path& DropFlowController::destination() const noexcept {
    return destination_;
}

const std::optional<DropMenuModel>& DropFlowController::menu() const noexcept {
    return menu_;
}

const std::optional<DestinationLayout>& DropFlowController::selected_layout() const noexcept {
    return selected_layout_;
}

DestinationValidation DropFlowController::choose_destination(
    const std::filesystem::path& destination) {
    std::vector<std::filesystem::path> sources;
    sources.reserve(items_.size());
    for (const auto& item : items_) {
        sources.push_back(item.source);
    }

    const auto validation = DestinationCatalog::validate(sources, destination);
    if (validation != DestinationValidation::Valid || items_.empty()) {
        return items_.empty() ? DestinationValidation::Empty : validation;
    }

    destination_ = destination;
    menu_ = builder_.build(items_, destination_);
    selected_layout_.reset();
    stage_ = DropFlowStage::Layout;
    return DestinationValidation::Valid;
}

bool DropFlowController::choose_layout(const DestinationLayout layout) noexcept {
    if (stage_ != DropFlowStage::Layout || !menu_) {
        return false;
    }
    selected_layout_ = layout;
    stage_ = DropFlowStage::Ready;
    return true;
}

bool DropFlowController::back() noexcept {
    if (stage_ == DropFlowStage::Ready) {
        selected_layout_.reset();
        stage_ = DropFlowStage::Layout;
        return true;
    }
    if (stage_ == DropFlowStage::Layout) {
        destination_.clear();
        menu_.reset();
        selected_layout_.reset();
        stage_ = DropFlowStage::Destination;
        return true;
    }
    return false;
}

std::optional<CopyJob> DropFlowController::make_job(
    const std::uint64_t job_id,
    const FileOperation operation) const {
    if (stage_ != DropFlowStage::Ready || !selected_layout_ || destination_.empty() || items_.empty()) {
        return std::nullopt;
    }
    return builder_.make_job(items_, destination_, *selected_layout_, job_id, operation);
}

} // namespace velocitycopy
