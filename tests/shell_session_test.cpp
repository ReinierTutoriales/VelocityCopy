#include "velocitycopy/shell_session.hpp"
int wmain() {
    using namespace velocitycopy;
    ShellSession session;
    ShellRequest r;
    if (session.dispatch(r).job) return 1;
    r.action = ShellAction::Transfer;
    r.sources = {L"C:\\Source\\a.txt", L"C:\\Source\\Folder"};
    r.destination = L"D:\\Target";
    for (auto op : {FileOperation::Copy, FileOperation::Move}) {
        r.operation = op;
        const auto result = session.dispatch(r);
        if (result.status != ShellDispatchStatus::Accepted || !result.job || !result.show_window ||
            result.job->sources != r.sources || result.job->destination != r.destination ||
            result.job->operation != op || result.job->layout != r.layout) return 2;
    }
    r.sources.clear();
    if (session.dispatch(r).status != ShellDispatchStatus::InvalidRequest) return 3;
    return 0;
}
