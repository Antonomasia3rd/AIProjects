#pragma once
// Private same-executable capture protocol. No Windows APIs or sidecar I/O.
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>
#include <vector>

namespace aip { namespace capture {
inline constexpr const wchar_t* HelperArgument = L"--ds-capture-window";
inline constexpr std::uint32_t ReplyMagic = 0x43505344; // DSPC
inline constexpr std::uint16_t ReplyVersion = 1;
inline constexpr std::uint64_t MaximumBitmapBytes = 512ull * 1024 * 1024;
struct HelperRequest {
    std::uintptr_t window = 0, outputHandle = 0, replyHandle = 0;
    std::uint32_t windowProcessId = 0;
    std::int32_t minimumWidth = 0, minimumHeight = 0;
    bool screenFallback = false;
    std::uint64_t requestId = 0;
};
enum class Status : std::uint32_t {
    Success = 0, CaptureFailed, InvalidRequest, WindowChanged,
    TimedOut, Canceled, ProcessFailed, InvalidReply, InvalidBitmap
};
inline constexpr std::uint32_t RejectedBlank = 1, RejectedTooSmall = 2;
struct Reply {
    std::uint32_t magic = ReplyMagic;
    std::uint16_t version = ReplyVersion, bytes = 56;
    std::uint64_t requestId = 0;
    Status status = Status::CaptureFailed;
    std::uint32_t flags = 0;
    std::int32_t width = 0, height = 0, minimumWidth = 0, minimumHeight = 0;
    std::uint32_t error = 0, reserved[3]{};
};
static_assert(sizeof(Reply) == 56, "capture reply ABI must stay fixed and smaller than a pipe buffer");
inline Reply MakeReply(const HelperRequest& request, Status status, std::uint32_t error = 0) noexcept {
    Reply reply; reply.requestId = request.requestId; reply.status = status; reply.error = error;
    reply.minimumWidth = request.minimumWidth; reply.minimumHeight = request.minimumHeight;
    return reply;
}
inline bool IsHelperArgument(const std::wstring& value) {
    const std::wstring expected = HelperArgument;
    if (value.size() != expected.size()) return false;
    for (std::size_t i = 0; i < value.size(); ++i) if (towlower(value[i]) != expected[i]) return false;
    return true;
}
inline bool ParseUnsigned(const std::wstring& value, std::uint64_t maximum, std::uint64_t& output) noexcept {
    if (value.empty()) return false;
    std::uint64_t next = 0;
    for (wchar_t character : value) {
        if (character < L'0' || character > L'9') return false;
        const auto digit = static_cast<unsigned>(character - L'0');
        if (digit > maximum || next > (maximum - digit) / 10) return false;
        next = next * 10 + digit;
    }
    output = next; return true;
}
// args includes argv[0]. recognized is true if the reserved switch occurs
// anywhere, so malformed/combined helper invocations fail before normal startup.
inline bool ParseHelperArguments(const std::vector<std::wstring>& args, HelperRequest& request, bool& recognized) {
    recognized = false;
    for (std::size_t i = 1; i < args.size(); ++i) if (IsHelperArgument(args[i])) recognized = true;
    if (!recognized) return false;
    if (args.size() != 10 || !IsHelperArgument(args[1])) return false;
    std::uint64_t values[8]{};
    const std::uint64_t pointerMaximum = (std::numeric_limits<std::uintptr_t>::max)();
    const std::uint64_t limits[] = {pointerMaximum, 0xffffffffull, pointerMaximum, pointerMaximum, 32768, 32768, 1,
        (std::numeric_limits<std::uint64_t>::max)()};
    for (unsigned i = 0; i < 8; ++i) if (!ParseUnsigned(args[i + 2], limits[i], values[i])) return false;
    for (unsigned i : {0u, 1u, 2u, 3u, 4u, 5u, 7u}) if (!values[i]) return false;
    if (values[2] == values[3]) return false;
    HelperRequest next;
    next.window = static_cast<std::uintptr_t>(values[0]); next.windowProcessId = static_cast<std::uint32_t>(values[1]);
    next.outputHandle = static_cast<std::uintptr_t>(values[2]); next.replyHandle = static_cast<std::uintptr_t>(values[3]);
    next.minimumWidth = static_cast<std::int32_t>(values[4]); next.minimumHeight = static_cast<std::int32_t>(values[5]);
    next.screenFallback = values[6] != 0; next.requestId = values[7]; request = next; return true;
}
inline bool ValidReply(const HelperRequest& request, const Reply& reply) noexcept {
    return reply.magic == ReplyMagic && reply.version == ReplyVersion && reply.bytes == sizeof(Reply) &&
        reply.requestId == request.requestId && reply.minimumWidth == request.minimumWidth && reply.minimumHeight == request.minimumHeight &&
        static_cast<std::uint32_t>(reply.status) <= static_cast<std::uint32_t>(Status::InvalidBitmap) &&
        (reply.flags & ~(RejectedBlank | RejectedTooSmall)) == 0 && reply.reserved[0] == 0 && reply.reserved[1] == 0 && reply.reserved[2] == 0;
}
inline std::wstring HelperArguments(const HelperRequest& request) {
    return std::wstring(HelperArgument) + L" " + std::to_wstring(request.window) + L" " + std::to_wstring(request.windowProcessId) +
        L" " + std::to_wstring(request.outputHandle) + L" " + std::to_wstring(request.replyHandle) +
        L" " + std::to_wstring(request.minimumWidth) + L" " + std::to_wstring(request.minimumHeight) +
        L" " + std::to_wstring(request.screenFallback ? 1 : 0) + L" " + std::to_wstring(request.requestId);
}
} }
