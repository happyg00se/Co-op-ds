#pragma once
// DSCP — DSCoop relay Control Protocol v1. Plain UDP datagrams exchanged between a client's
// ENet socket and the relay. They ride on the same socket as ENet traffic and are told apart
// by the 4-byte magic; the ENet host's `intercept` callback consumes them.
// The Python relay (relay/relay.py) implements the server side of exactly this format.
//
//   all:      'D' 'S' 'C' 'P'  u8 version(=1)  u8 op  body
//   CREATE    {u8 maxGuests, u32 clientVersion}                        client -> relay:control
//   CREATED   {char code[6], u64 hostToken, u16 hostPort, u8 n, u16 guestPorts[n]}  relay -> host
//   JOIN      {char code[6], u32 clientVersion}                        guest -> relay:control
//   JOINED    {u16 hostPort, u8 slot, u64 slotToken}                   relay -> guest
//   ERROR     {u8 code, str8 text}                                     relay -> client
//   PING      {u64 token, u8 slot}       host -> relay:guestPort[slot] (hostToken) | guest -> relay:hostPort (slotToken)
//   PONG      {u8 slot, u32 serverTimeMs}                              relay -> client
//   CLOSE     {u64 hostToken}                                          host -> relay:control
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dsproto/byteio.hpp"

namespace dsproto::relay {

constexpr std::array<uint8_t, 4> kMagic{'D', 'S', 'C', 'P'};
constexpr uint8_t kVersion = 1;
constexpr size_t kRoomCodeLen = 6;
constexpr uint8_t kMaxGuests = 7;  // 8 players = host + 7
constexpr uint16_t kDefaultControlPort = 27000;

enum class Op : uint8_t { Create = 1, Created = 2, Join = 3, Joined = 4, Error = 5, Ping = 6, Pong = 7, Close = 8 };

enum class ErrorCode : uint8_t {
    BadRequest = 1,
    RoomNotFound = 2,
    RoomFull = 3,
    NoCapacity = 4,
    VersionMismatch = 5,
    BadToken = 6,
};

inline const char* errorText(ErrorCode c) {
    switch (c) {
        case ErrorCode::BadRequest: return "bad request";
        case ErrorCode::RoomNotFound: return "room not found";
        case ErrorCode::RoomFull: return "room is full";
        case ErrorCode::NoCapacity: return "relay has no free rooms";
        case ErrorCode::VersionMismatch: return "relay/client version mismatch";
        case ErrorCode::BadToken: return "bad token";
    }
    return "unknown";
}

struct Created {
    std::string code;
    uint64_t hostToken = 0;
    uint16_t hostPort = 0;
    std::vector<uint16_t> guestPorts;
};
struct Joined {
    uint16_t hostPort = 0;
    uint8_t slot = 0;
    uint64_t slotToken = 0;
};
struct Error {
    ErrorCode code = ErrorCode::BadRequest;
    std::string text;
};
struct Pong {
    uint8_t slot = 0;
    uint32_t serverTimeMs = 0;
};

inline bool isControlPacket(std::span<const uint8_t> d) {
    return d.size() >= 6 && d[0] == 'D' && d[1] == 'S' && d[2] == 'C' && d[3] == 'P' && d[4] == kVersion;
}
inline std::optional<Op> peekOp(std::span<const uint8_t> d) {
    if (!isControlPacket(d)) return std::nullopt;
    if (d[5] < 1 || d[5] > 8) return std::nullopt;
    return static_cast<Op>(d[5]);
}

inline ByteWriter header(Op op) {
    ByteWriter w(32);
    w.bytes(kMagic); w.u8(kVersion); w.u8(static_cast<uint8_t>(op));
    return w;
}

inline std::vector<uint8_t> encodeCreate(uint8_t maxGuests, uint32_t clientVersion) {
    auto w = header(Op::Create); w.u8(maxGuests); w.u32(clientVersion); return w.take();
}
inline std::vector<uint8_t> encodeJoin(const std::string& code, uint32_t clientVersion) {
    auto w = header(Op::Join);
    for (size_t i = 0; i < kRoomCodeLen; ++i) w.u8(i < code.size() ? static_cast<uint8_t>(code[i]) : 0);
    w.u32(clientVersion);
    return w.take();
}
inline std::vector<uint8_t> encodePing(uint64_t token, uint8_t slot) {
    auto w = header(Op::Ping); w.u64(token); w.u8(slot); return w.take();
}
inline std::vector<uint8_t> encodeClose(uint64_t hostToken) {
    auto w = header(Op::Close); w.u64(hostToken); return w.take();
}

inline bool decodeCreated(std::span<const uint8_t> d, Created& out) {
    if (peekOp(d) != Op::Created) return false;
    ByteReader r(d.data() + 6, d.size() - 6);
    char code[kRoomCodeLen];
    r.bytes(reinterpret_cast<uint8_t*>(code), kRoomCodeLen);
    out.code.assign(code, kRoomCodeLen);
    out.hostToken = r.u64(); out.hostPort = r.u16();
    const uint8_t n = r.u8();
    if (n > kMaxGuests) return false;
    out.guestPorts.clear();
    for (uint8_t i = 0; i < n; ++i) out.guestPorts.push_back(r.u16());
    return r.ok();
}
inline bool decodeJoined(std::span<const uint8_t> d, Joined& out) {
    if (peekOp(d) != Op::Joined) return false;
    ByteReader r(d.data() + 6, d.size() - 6);
    out.hostPort = r.u16(); out.slot = r.u8(); out.slotToken = r.u64();
    return r.ok();
}
inline bool decodeError(std::span<const uint8_t> d, Error& out) {
    if (peekOp(d) != Op::Error) return false;
    ByteReader r(d.data() + 6, d.size() - 6);
    out.code = static_cast<ErrorCode>(r.u8()); out.text = r.str8();
    return r.ok();
}
inline bool decodePong(std::span<const uint8_t> d, Pong& out) {
    if (peekOp(d) != Op::Pong) return false;
    ByteReader r(d.data() + 6, d.size() - 6);
    out.slot = r.u8(); out.serverTimeMs = r.u32();
    return r.ok();
}

/// Room codes use an alphabet without look-alikes (no 0/O, 1/I/L).
constexpr std::string_view kRoomCodeAlphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
inline std::string normalizeRoomCode(std::string s) {
    std::string out;
    for (char c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c == ' ' || c == '-') continue;
        out.push_back(c);
    }
    return out;
}
inline bool isValidRoomCode(std::string_view s) {
    if (s.size() != kRoomCodeLen) return false;
    for (char c : s) if (kRoomCodeAlphabet.find(c) == std::string_view::npos) return false;
    return true;
}

}  // namespace dsproto::relay
