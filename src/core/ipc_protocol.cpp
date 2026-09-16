#include "velocitycopy/ipc_protocol.hpp"

#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

namespace velocitycopy {
namespace {

void append_u32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

bool read_u32(std::span<const std::uint8_t> bytes, std::size_t& offset, std::uint32_t& value) noexcept {
    if (offset + 4 > bytes.size()) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
    offset += 4;
    return true;
}

bool append_path(std::vector<std::uint8_t>& out, const std::filesystem::path& path) {
    const auto& text = path.native();
    if (text.size() > kMaxShellPathChars || text.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    append_u32(out, static_cast<std::uint32_t>(text.size()));
    const auto* raw = reinterpret_cast<const std::uint8_t*>(text.data());
    out.insert(out.end(), raw, raw + (text.size() * sizeof(wchar_t)));
    return out.size() <= kMaxShellMessageBytes;
}

bool read_path(
    std::span<const std::uint8_t> bytes,
    std::size_t& offset,
    std::filesystem::path& path) noexcept {
    std::uint32_t chars = 0;
    if (!read_u32(bytes, offset, chars) || chars > kMaxShellPathChars) {
        return false;
    }
    const auto byte_count = static_cast<std::size_t>(chars) * sizeof(wchar_t);
    if (offset + byte_count > bytes.size()) {
        return false;
    }
    std::wstring text(chars, L'\0');
    if (byte_count != 0) {
        std::memcpy(text.data(), bytes.data() + offset, byte_count);
    }
    offset += byte_count;
    path = std::filesystem::path(std::move(text));
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> serialize_shell_request(const ShellRequest& request) noexcept {
    try {
        if (!shell_request_valid(request) || request.sources.size() > kMaxShellSources) {
            return std::nullopt;
        }

        std::vector<std::uint8_t> out;
        out.reserve(256);
        append_u32(out, kShellProtocolMagic);
        append_u32(out, request.version);
        out.push_back(static_cast<std::uint8_t>(request.action));
        out.push_back(static_cast<std::uint8_t>(request.layout));
        out.push_back(0);
        out.push_back(0);
        append_u32(out, static_cast<std::uint32_t>(request.sources.size()));

        for (const auto& source : request.sources) {
            if (!append_path(out, source)) {
                return std::nullopt;
            }
        }
        if (!append_path(out, request.destination) || out.size() > kMaxShellMessageBytes) {
            return std::nullopt;
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ShellRequest> deserialize_shell_request(std::span<const std::uint8_t> bytes) noexcept {
    try {
        if (bytes.empty() || bytes.size() > kMaxShellMessageBytes) {
            return std::nullopt;
        }

        std::size_t offset = 0;
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint32_t source_count = 0;
        if (!read_u32(bytes, offset, magic) || magic != kShellProtocolMagic ||
            !read_u32(bytes, offset, version) || offset + 4 > bytes.size()) {
            return std::nullopt;
        }

        ShellRequest request{};
        request.version = version;
        request.action = static_cast<ShellAction>(bytes[offset++]);
        request.layout = static_cast<DestinationLayout>(bytes[offset++]);
        offset += 2;

        if (!read_u32(bytes, offset, source_count) || source_count > kMaxShellSources) {
            return std::nullopt;
        }
        request.sources.reserve(source_count);
        for (std::uint32_t i = 0; i < source_count; ++i) {
            std::filesystem::path source;
            if (!read_path(bytes, offset, source)) {
                return std::nullopt;
            }
            request.sources.push_back(std::move(source));
        }
        if (!read_path(bytes, offset, request.destination) || offset != bytes.size()) {
            return std::nullopt;
        }
        if (!shell_request_valid(request)) {
            return std::nullopt;
        }
        return request;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace velocitycopy
