#pragma once
#include <memory>

namespace velocitycopy {
// Resident Explorer keyboard adapter. No file I/O or clipboard polling.
class ExplorerPaste final {
public:
    ExplorerPaste();
    ~ExplorerPaste();
    ExplorerPaste(const ExplorerPaste&) = delete;
    ExplorerPaste& operator=(const ExplorerPaste&) = delete;
    bool start() noexcept;
    void stop() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
