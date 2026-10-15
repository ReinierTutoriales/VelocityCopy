#include "velocitycopy/ipc_protocol.hpp"
#include "velocitycopy/ipc_transport.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>
#include <thread>

using namespace std::chrono_literals;

namespace {
bool send_times_out_when_server_stops_reading(const bool small_frame = false) {
    const auto name = velocitycopy::shell_pipe_name();
    if (name.empty()) return false;

    HANDLE pipe = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        1024,
        1024,
        0,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    std::atomic_bool release{false};
    std::atomic_bool connected{false};
    std::jthread server([&] {
        const BOOL ok = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);
        connected.store(ok != FALSE, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
    });

    velocitycopy::ShellRequest large{};
    large.action = velocitycopy::ShellAction::Transfer;
    large.operation = velocitycopy::FileOperation::Copy;
    large.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    large.destination = L"C:\\Destination";
    const std::wstring padding(small_frame ? 100 : 30000, L'x');
    for (int index = 0; index < (small_frame ? 1 : 200); ++index) {
        large.sources.emplace_back(
            L"C:\\" + padding + L"\\file-" + std::to_wstring(index));
    }

    const auto start = std::chrono::steady_clock::now();
    const auto sent = velocitycopy::send_shell_request(large, 100);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    release.store(true, std::memory_order_release);
    CancelSynchronousIo(static_cast<HANDLE>(server.native_handle()));
    server.join();
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    return !sent &&
        sent.native_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_SEM_TIMEOUT)) &&
        sent.delivery_ambiguous &&
        connected.load(std::memory_order_acquire) &&
        elapsed < 2s;
}

bool stop_with_partial_frame(const bool payload_started) {
    velocitycopy::ShellIpcServer server;
    if (!server.valid()) return false;
    std::optional<velocitycopy::ShellRequest> result;
    std::jthread receiver([&] { result = server.receive(); });
    const auto name = velocitycopy::shell_pipe_name();
    HANDLE client = INVALID_HANDLE_VALUE;
    if (WaitNamedPipeW(name.c_str(), 2000)) {
        client = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (client == INVALID_HANDLE_VALUE) { server.stop(); receiver.join(); return false; }
    const std::uint32_t size = 32;
    const std::uint8_t byte = 1;
    DWORD written = 0;
    bool sent = true;
    if (payload_started) sent = WriteFile(client, &size, sizeof(size), &written, nullptr) != FALSE;
    sent = sent && WriteFile(client, &byte, sizeof(byte), &written, nullptr) != FALSE;
    std::this_thread::sleep_for(25ms);
    server.stop();
    const DWORD finished = WaitForSingleObject(static_cast<HANDLE>(receiver.native_handle()), 1000);
    // Release the client even on failure so this regression fails without
    // hanging the test runner against the old synchronous implementation.
    CloseHandle(client);
    receiver.join();
    return sent && finished == WAIT_OBJECT_0 && !result && server.stopping();
}

bool stalled_frame_recovers(const velocitycopy::ShellRequest& request) {
    velocitycopy::ShellIpcServer server;
    if (!server.valid()) return false;
    std::optional<velocitycopy::ShellRequest> result;
    std::jthread receiver([&] { result = server.receive(); });
    const auto name = velocitycopy::shell_pipe_name();
    if (!WaitNamedPipeW(name.c_str(), 2000)) { server.stop(); receiver.join(); return false; }
    HANDLE client = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (client == INVALID_HANDLE_VALUE) { server.stop(); receiver.join(); return false; }
    const std::uint32_t size = 32;
    DWORD written = 0;
    const bool sent = WriteFile(client, &size, sizeof(size), &written, nullptr) != FALSE;
    const DWORD finished = WaitForSingleObject(static_cast<HANDLE>(receiver.native_handle()), 7000);
    CloseHandle(client);
    receiver.join();
    if (!sent || finished != WAIT_OBJECT_0 || result || server.stopping() || !server.valid()) return false;
    std::jthread recovery([&] { result = server.receive(); });
    const auto accepted = velocitycopy::send_shell_request(request, 2000);
    if (!accepted) server.stop();
    recovery.join();
    return accepted && result && result->sources == request.sources;
}

bool large_frame_with_bounded_buffer() {
    velocitycopy::ShellIpcServer server;
    if (!server.valid()) return false;
    velocitycopy::ShellRequest request;
    request.action = velocitycopy::ShellAction::Transfer;
    request.destination = L"C:\\destination";
    const std::wstring name(30000, L'x');
    for (int i = 0; i < 200; ++i) request.sources.emplace_back(L"C:\\" + name + std::to_wstring(i));
    std::optional<velocitycopy::ShellRequest> result;
    std::jthread receiver([&] { result = server.receive(); });
    const auto sent = velocitycopy::send_shell_request(request, 3000);
    if (!sent) server.stop();
    receiver.join();
    return sent && result && result->sources == request.sources && result->destination == request.destination;
}

bool concurrent_clients_preserve_distinct_requests() {
    constexpr int count = 4;
    velocitycopy::ShellIpcServer server;
    if (!server.valid()) return false;

    std::atomic_int received_count{0};
    std::atomic_uint received_mask{0};
    std::atomic_bool valid{true};
    std::jthread receiver([&] {
        for (int i = 0; i < count; ++i) {
            const auto message = server.receive();
            if (!message || message->sources.size() != 1) {
                valid.store(false, std::memory_order_release);
                return;
            }
            const auto source = message->sources.front().wstring();
            if (!source.starts_with(L"C:\\Concurrent\\file-")) {
                valid.store(false, std::memory_order_release);
                return;
            }
            const auto suffix = source.substr(std::wstring_view(L"C:\\Concurrent\\file-").size());
            if (suffix.size() != 1 || suffix[0] < L'0' || suffix[0] >= L'0' + count) {
                valid.store(false, std::memory_order_release);
                return;
            }
            const unsigned bit = 1u << (suffix[0] - L'0');
            if (received_mask.fetch_or(bit, std::memory_order_acq_rel) & bit) {
                valid.store(false, std::memory_order_release);
                return;
            }
            received_count.fetch_add(1, std::memory_order_release);
        }
    });

    std::atomic_int sent_count{0};
    std::vector<std::jthread> clients;
    clients.reserve(count);
    for (int i = 0; i < count; ++i) {
        clients.emplace_back([&, i] {
            velocitycopy::ShellRequest request{};
            request.action = velocitycopy::ShellAction::Transfer;
            request.destination = L"D:\\Backup";
            request.sources = {std::filesystem::path(L"C:\\Concurrent\\file-" + std::to_wstring(i))};
            if (velocitycopy::send_shell_request(request, 10000))
                sent_count.fetch_add(1, std::memory_order_release);
        });
        // Keep clients overlapping while allowing the one-instance server to
        // re-arm ConnectNamedPipe between successive arrivals.
        std::this_thread::sleep_for(50ms);
    }
    for (auto& client : clients) client.join();
    if (sent_count.load(std::memory_order_acquire) != count) server.stop();
    receiver.join();
    const bool passed = valid.load(std::memory_order_acquire) &&
        received_count.load(std::memory_order_acquire) == count &&
        received_mask.load(std::memory_order_acquire) == (1u << count) - 1 &&
        sent_count.load(std::memory_order_acquire) == count;
    if (!passed) {
        std::cerr << "IPC concurrent clients: sent=" << sent_count.load()
                  << " received=" << received_count.load()
                  << " unique_mask=" << received_mask.load()
                  << " valid=" << valid.load() << "\n";
    }
    return passed;
}

bool send_malformed_message() {
    const auto name = velocitycopy::shell_pipe_name();
    if (name.empty() || !WaitNamedPipeW(name.c_str(), 2000)) return false;
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    const std::uint32_t size = 4;
    const std::uint32_t garbage = 0xdeadbeefu;
    DWORD written = 0;
    const bool ok = WriteFile(pipe, &size, sizeof(size), &written, nullptr) && written == sizeof(size) &&
        WriteFile(pipe, &garbage, sizeof(garbage), &written, nullptr) && written == sizeof(garbage);
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return ok;
}
} // namespace

int run_test() {
    using namespace velocitycopy;

    ShellRequest request{};
    request.action = ShellAction::Transfer;
    request.operation = FileOperation::Move;
    request.sources = {
        std::filesystem::path(L"C:\\Test\\Novela\\capitulo1.mkv"),
        std::filesystem::path(L"C:\\Test\\Novela\\capitulo2.mkv")};
    request.destination = L"D:\\Backup";
    request.layout = DestinationLayout::PreserveSourceFolder;

    const auto encoded = serialize_shell_request(request);
    if (!encoded) return 1;
    const auto decoded = deserialize_shell_request(*encoded);
    if (!decoded || decoded->sources != request.sources || decoded->destination != request.destination ||
        decoded->operation != request.operation || decoded->action != request.action || decoded->layout != request.layout) return 2;

    // Version 3 carries a stable nonzero request identity. Version 2 remains
    // byte-for-byte compatible with existing installed shell clients.
    ShellRequest identified = request;
    identified.version = 3;
    identified.request_id = 0x12345678abcdef01ull;
    const auto encoded_v3 = serialize_shell_request(identified);
    if (!encoded_v3 || encoded_v3->size() != encoded->size() + 8) return 22;
    const auto decoded_v3 = deserialize_shell_request(*encoded_v3);
    if (!decoded_v3 || decoded_v3->version != 3 || decoded_v3->request_id != identified.request_id ||
        decoded_v3->sources != request.sources || decoded_v3->destination != request.destination) return 23;
    identified.request_id = 0;
    if (serialize_shell_request(identified)) return 24;
    identified.version = 2;
    identified.request_id = 42;
    if (serialize_shell_request(identified)) return 25;
    auto truncated_v3 = *encoded_v3;
    truncated_v3.resize(11);
    if (deserialize_shell_request(truncated_v3)) return 26;

    auto corrupted = *encoded;
    corrupted[0] ^= 0xffu;
    if (deserialize_shell_request(corrupted)) return 3;

    SingleInstance first;
    SingleInstance second;
    if (!first.valid() || !first.primary() || !second.valid() || second.primary()) return 4;

    {
    ShellIpcServer server;
    if (!server.valid()) return 5;

    std::optional<ShellRequest> received;
    std::jthread receiver([&] { received = server.receive(); });
    if (!send_shell_request(request, 2000)) return 6;
    receiver.join();
    if (!received || received->sources != request.sources || received->destination != request.destination ||
        received->operation != request.operation || received->action != request.action || received->layout != request.layout) return 7;

    // The transport must preserve v3 identity, not just the serializer.
    std::optional<ShellRequest> identified_received;
    std::jthread identified_receiver([&] { identified_received = server.receive_and_ack([](const ShellRequest&) { return true; }); });
    ShellRequest identified_wire = request;
    identified_wire.version = 3;
    identified_wire.request_id = 0x9a7b6c5d4e3f2011ull;
    const auto identified_send = send_shell_request(identified_wire, 2000);
    identified_receiver.join();
    if (!identified_send || !identified_received || identified_received->version != 3 ||
        identified_received->request_id != identified_wire.request_id ||
        identified_received->sources != request.sources ||
        identified_received->destination != request.destination) return 27;

    // A v3 rejection is explicit and must not be reported as admission.
    std::optional<ShellRequest> rejected_received;
    std::jthread rejecting_receiver([&] {
        rejected_received = server.receive_and_ack([](const ShellRequest&) { return false; });
    });
    const auto rejected_send = send_shell_request(identified_wire, 2000);
    rejecting_receiver.join();
    if (rejected_send || rejected_send.delivery_ambiguous ||
        rejected_send.native_code != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) ||
        !rejected_received || rejected_received->request_id != identified_wire.request_id) return 28;

    // The server must continue to admit the next request after a rejection.
    std::optional<ShellRequest> after_rejection;
    std::jthread accepting_again([&] {
        after_rejection = server.receive_and_ack([](const ShellRequest&) { return true; });
    });
    const auto accepted_again = send_shell_request(identified_wire, 2000);
    accepting_again.join();
    if (!accepted_again || !after_rejection ||
        after_rejection->request_id != identified_wire.request_id) return 29;

    // Malformed client data is a protocol rejection, not a transport shutdown.
    std::optional<ShellRequest> malformed_result;
    std::jthread malformed_receiver([&] { malformed_result = server.receive(); });
    if (!send_malformed_message()) return 8;
    malformed_receiver.join();
    if (malformed_result || server.stopping() || !server.valid()) return 9;

    // The same server must accept the next valid activation after malformed input.
    std::optional<ShellRequest> recovered;
    std::jthread recovery_receiver([&] { recovered = server.receive(); });
    if (!send_shell_request(request, 2000)) return 10;
    recovery_receiver.join();
    if (!recovered || recovered->sources != request.sources || recovered->destination != request.destination) return 11;

    // Shutdown contract: stop() must wake a thread blocked in ConnectNamedPipe
    // without polling or detaching the receiver thread.
    std::atomic_bool stop_receiver_returned{false};
    std::optional<ShellRequest> stop_result;
    std::jthread stop_receiver([&] {
        stop_result = server.receive();
        stop_receiver_returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(25ms);
    if (stop_receiver_returned.load(std::memory_order_acquire)) {
        stop_receiver.join();
        return 12;
    }

    server.stop();
    stop_receiver.join();
    if (!server.stopping() || !stop_receiver_returned.load(std::memory_order_acquire) || stop_result) return 13;
    }

    // A shell extension runs in Explorer's process. A resident VelocityCopy
    // instance that accepts the pipe connection but stops reading must not be
    // able to block Explorer indefinitely.
    if (!send_times_out_when_server_stops_reading()) return 14;
    if (!stop_with_partial_frame(false)) return 15;
    if (!stop_with_partial_frame(true)) return 16;
    if (!stalled_frame_recovers(request)) return 17;
    if (!large_frame_with_bounded_buffer()) return 18;
    if (!send_times_out_when_server_stops_reading(true)) return 19;
    if (!concurrent_clients_preserve_distinct_requests()) return 20;

    // No pipe exists after server teardown. This failure is known to occur
    // before any frame write, so a secondary launch may safely retry it.
    const auto unavailable = send_shell_request(request, 100);
    if (unavailable || unavailable.delivery_ambiguous ||
        unavailable.native_code != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) return 21;

    std::wcout << L"VelocityCopy IPC test passed.\n";
    return 0;
}

int wmain() {
    const int result = run_test();
    if (result) std::cerr << "IPC failure at check " << result << "\n";
    return result;
}
