#pragma once
#include "QueueItem.g.h"
#include <filesystem>
#include <cstdint>

namespace winrt::VelocityCopyUI::implementation {
struct QueueItem : QueueItemT<QueueItem> {
    QueueItem() = default;
    QueueItem(const std::filesystem::path& source, const std::uint64_t id)
        : id_(id), name_(source.filename().wstring()),
          location_(source.parent_path().wstring()), full_path_(source.wstring()) {}
    std::uint64_t Id() const noexcept { return id_; }
    hstring Name() const { return name_; }
    hstring Location() const { return location_; }
    hstring FullPath() const { return full_path_; }
private:
    std::uint64_t id_{};
    hstring name_;
    hstring location_;
    hstring full_path_;
};
}
namespace winrt::VelocityCopyUI::factory_implementation {
struct QueueItem : QueueItemT<QueueItem, implementation::QueueItem> {};
}
