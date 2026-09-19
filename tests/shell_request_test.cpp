#include "velocitycopy/ipc_protocol.hpp"
int main() {
    using namespace velocitycopy;
    ShellRequest r;
    if (!shell_request_valid(r)) return 1;
    r.action = ShellAction::Transfer;
    if (shell_request_valid(r)) return 2;
    r.sources = {L"C:\\Source\\a.txt"};
    r.destination = L"D:\\Target";
    r.operation = FileOperation::Move;
    if (!shell_request_valid(r)) return 3;
    auto bytes = serialize_shell_request(r);
    if (!bytes) return 4;
    auto decoded = deserialize_shell_request(*bytes);
    if (!decoded || decoded->operation != FileOperation::Move) return 5;
    auto bad = *bytes;
    bad[4] = 1;
    if (deserialize_shell_request(bad)) return 6; // retired wire contract
    bad = *bytes; bad[10] = 255;
    if (deserialize_shell_request(bad)) return 7;
    bad = *bytes; bad[9] = 255;
    if (deserialize_shell_request(bad)) return 8;
    bad = *bytes; bad[11] = 1;
    if (deserialize_shell_request(bad)) return 9;
    for (size_t n = 0; n < bytes->size(); ++n)
        if (deserialize_shell_request(std::span(*bytes).first(n))) return 10;
    r.sources = {L"relative.txt"};
    if (shell_request_valid(r)) return 11;
    r.sources = {std::wstring(L"C:\\a\0suffix", 11)};
    if (shell_request_valid(r)) return 12;
    r.action = static_cast<ShellAction>(255);
    if (shell_request_valid(r)) return 13;
    return 0;
}
