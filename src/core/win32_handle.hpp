#pragma once
#include <windows.h>

namespace velocitycopy::detail {
class Win32Handle final {
public:
    explicit Win32Handle(HANDLE value) noexcept : value_(value) {}
    ~Win32Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Win32Handle(const Win32Handle&) = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};
}
