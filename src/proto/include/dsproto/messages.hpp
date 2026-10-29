#pragma once
// DSCoop network protocol v0 — message definitions and (de)serialization.
// Transport: ENet. Every ENet packet payload = [u8 MsgType][body]. See docs/protocol-v0.md.
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dsproto/byteio.hpp"
#include "dsproto/quant.hpp"
#include "dsproto/types.hpp"

namespace dsproto {

constexpr uint16_t kProtocolVersion = 0;
constexpr uint8_t kMaxPlayers = 8;
constexpr uint8_t kInvalidPlayerId = 0xFF;
constexpr uint32_t kDefaultTickRate = 30;
constexpr size_t kMaxNickBytes = 24;
constexpr size_t kMaxChatBytes = 240;

// ENet channels
constexpr uint8_t kChannelControl = 0;  // reliable: handshake, join/leave, chat, settings
constexpr uint8_t kChannelState = 1;    // unreliable sequenced: PlayerState / WorldSnapshot
constexpr uint8_t kChannelWorld = 2;    // reliable: story/cargo/structures (stage 3+)
constexpr size_t kChannelCount = 3;

enum class MsgType : uint8_t {
    Invalid = 0,
    Hello = 1,          // C->H
    Welcome = 2,        // H->C
    Reject = 3,         // H->C
    PlayerJoined = 4,   // H->C
    PlayerLeft = 5,     // H->C
    PlayerState = 6,    // C->H
    WorldSnapshot = 7,  // H->C
    Ping = 8,           // C->H
    Pong = 9,           // H->C
    Chat = 10,          // both
    HostSettings = 11,  // H->C
    Max_
};

inline const char* msgTypeName(MsgType t) {
    switch (t) {
        case MsgType::Hello: return "Hello";
        case MsgType::Welcome: return "Welcome";
        case MsgType::Reject: return "Reject";
        case MsgType::PlayerJoined: return "PlayerJoined";
        case MsgType::PlayerLeft: return "PlayerLeft";
        case MsgType::PlayerState: return "PlayerState";
        case MsgType::WorldSnapshot: return "WorldSnapshot";
        case MsgType::Ping: return "Ping";
        case MsgType::Pong: return "Pong";
        case MsgType::Chat: return "Chat";
        case MsgType::HostSettings: return "HostSettings";
        default: return "Invalid";
    }
}

enum class RejectReason : uint8_t {
    Unknown = 0,
    ServerFull = 1,
    ProtocolMismatch = 2,
    ClientVersionMismatch = 3,
    GameBuildMismatch = 4,
    BadNick = 5,
    Kicked = 6,
    HostShutdown = 7,
};

enum HostFlags : uint32_t {
    HF_PvP = 1 << 0,
    HF_DirectorsCutContent = 1 << 1,
    HF_SyncWeather = 1 << 2,
    HF_SyncTimeOfDay = 1 << 3,
    HF_FriendlyFire = 1 << 4,
};

enum ClientFeatures : uint32_t {
    CF_FakeClient = 1 << 0,  // test client without the game attached
    CF_HasOffsets = 1 << 1,  // real game memory is readable (offsets resolved)
};

struct PlayerInfo {
    uint8_t id = kInvalidPlayerId;
    std::string nick;
};

// ---------------------------------------------------------------------------
struct Hello {
    uint16_t protocolVersion = kProtocolVersion;
    uint32_t clientVersion = 0;  // DSCOOP_VERSION_INT
    uint32_t gameBuild = 0;      // hash of the game exe (0 = unknown / fake client)
    uint32_t features = 0;       // ClientFeatures
    std::string nick;
};

struct Welcome {
    uint8_t playerId = kInvalidPlayerId;
    uint32_t hostTick = 0;
    uint8_t tickRate = kDefaultTickRate;
    uint64_t sessionId = 0;
    uint32_t hostFlags = 0;
    uint32_t hostVersion = 0;
    std::vector<PlayerInfo> players;  // players already in the session (including host, id 0)
};

struct Reject {
    RejectReason reason = RejectReason::Unknown;
    std::string text;
};

struct PlayerJoined {
    PlayerInfo player;
};

struct PlayerLeft {
    uint8_t id = kInvalidPlayerId;
    uint8_t reason = 0;  // 0 = left, 1 = timeout, 2 = kicked
};

/// Guest -> host, unreliable, every client frame or at 30 Hz, whichever is lower.
struct PlayerStateMsg {
    uint32_t ackedSnapshotTick = 0;  // newest WorldSnapshot tick the guest decoded (for delta baselines)
    PlayerState state;
};

struct Ping {
    uint32_t clientTimeMs = 0;
};
struct Pong {
    uint32_t clientTimeMs = 0;
    uint32_t hostTimeMs = 0;
    uint32_t hostTick = 0;
};

struct Chat {
    uint8_t fromId = kInvalidPlayerId;
    std::string text;
};

struct HostSettings {
    uint32_t hostFlags = 0;
    uint8_t tickRate = kDefaultTickRate;
};

// ---------------------------------------------------------------------------
// PlayerState wire format (full): 4+12+2+2+6+2+2+1+1 = 32 bytes
inline void writePlayerStateFull(ByteWriter& w, const PlayerState& s) {
    w.u32(s.tick);
    w.f32(s.pos.x); w.f32(s.pos.y); w.f32(s.pos.z);
    w.u16(quant::angleToU16(s.yaw));
    w.i16(quant::toI16(s.pitch, 10000.0f));
    w.i16(quant::toI16(s.vel.x, 256.0f)); w.i16(quant::toI16(s.vel.y, 256.0f)); w.i16(quant::toI16(s.vel.z, 256.0f));
    w.u16(s.animState);
    w.u16(s.flags);
    w.u8(quant::unitToU8(s.health));
    w.u8(quant::unitToU8(s.stamina));
}
inline PlayerState readPlayerStateFull(ByteReader& r) {
    PlayerState s;
    s.tick = r.u32();
    s.pos.x = r.f32(); s.pos.y = r.f32(); s.pos.z = r.f32();
    s.yaw = quant::u16ToAngle(r.u16());
    s.pitch = quant::fromI16(r.i16(), 10000.0f);
    s.vel.x = quant::fromI16(r.i16(), 256.0f); s.vel.y = quant::fromI16(r.i16(), 256.0f); s.vel.z = quant::fromI16(r.i16(), 256.0f);
    s.animState = r.u16();
    s.flags = r.u16();
    s.health = quant::u8ToUnit(r.u8());
    s.stamina = quant::u8ToUnit(r.u8());
    return s;
}

/// Quantize a state through the wire format so host and guest compare identical values.
inline PlayerState quantize(const PlayerState& s) {
    ByteWriter w(32);
    writePlayerStateFull(w, s);
    ByteReader r(w.data());
    return readPlayerStateFull(r);
}

// ---------------------------------------------------------------------------
// encode()/decode() per message. encode() prefixes the MsgType byte.
inline std::vector<uint8_t> encode(const Hello& m) {
    ByteWriter w(48);
    w.u8(static_cast<uint8_t>(MsgType::Hello));
    w.u16(m.protocolVersion); w.u32(m.clientVersion); w.u32(m.gameBuild); w.u32(m.features);
    w.str8(m.nick.substr(0, kMaxNickBytes));
    return w.take();
}
inline bool decode(ByteReader& r, Hello& m) {
    m.protocolVersion = r.u16(); m.clientVersion = r.u32(); m.gameBuild = r.u32(); m.features = r.u32();
    m.nick = r.str8();
    return r.ok();
}

inline std::vector<uint8_t> encode(const Welcome& m) {
    ByteWriter w(64);
    w.u8(static_cast<uint8_t>(MsgType::Welcome));
    w.u8(m.playerId); w.u32(m.hostTick); w.u8(m.tickRate); w.u64(m.sessionId); w.u32(m.hostFlags); w.u32(m.hostVersion);
    w.u8(static_cast<uint8_t>(m.players.size() > kMaxPlayers ? kMaxPlayers : m.players.size()));
    for (size_t i = 0; i < m.players.size() && i < kMaxPlayers; ++i) {
        w.u8(m.players[i].id);
        w.str8(m.players[i].nick.substr(0, kMaxNickBytes));
    }
    return w.take();
}
inline bool decode(ByteReader& r, Welcome& m) {
    m.playerId = r.u8(); m.hostTick = r.u32(); m.tickRate = r.u8(); m.sessionId = r.u64(); m.hostFlags = r.u32(); m.hostVersion = r.u32();
    const uint8_t n = r.u8();
    if (n > kMaxPlayers) return false;
    m.players.clear();
    for (uint8_t i = 0; i < n; ++i) {
        PlayerInfo p; p.id = r.u8(); p.nick = r.str8();
        m.players.push_back(std::move(p));
    }
    return r.ok();
}

inline std::vector<uint8_t> encode(const Reject& m) {
    ByteWriter w(32);
    w.u8(static_cast<uint8_t>(MsgType::Reject));
    w.u8(static_cast<uint8_t>(m.reason)); w.str8(m.text);
    return w.take();
}
inline bool decode(ByteReader& r, Reject& m) {
    m.reason = static_cast<RejectReason>(r.u8()); m.text = r.str8();
    return r.ok();
}

inline std::vector<uint8_t> encode(const PlayerJoined& m) {
    ByteWriter w(32);
    w.u8(static_cast<uint8_t>(MsgType::PlayerJoined));
    w.u8(m.player.id); w.str8(m.player.nick.substr(0, kMaxNickBytes));
    return w.take();
}
inline bool decode(ByteReader& r, PlayerJoined& m) {
    m.player.id = r.u8(); m.player.nick = r.str8();
    return r.ok();
}

inline std::vector<uint8_t> encode(const PlayerLeft& m) {
    ByteWriter w(4);
    w.u8(static_cast<uint8_t>(MsgType::PlayerLeft));
    w.u8(m.id); w.u8(m.reason);
    return w.take();
}
inline bool decode(ByteReader& r, PlayerLeft& m) {
    m.id = r.u8(); m.reason = r.u8();
    return r.ok();
}

inline std::vector<uint8_t> encode(const PlayerStateMsg& m) {
    ByteWriter w(40);
    w.u8(static_cast<uint8_t>(MsgType::PlayerState));
    w.u32(m.ackedSnapshotTick);
    writePlayerStateFull(w, m.state);
    return w.take();
}
inline bool decode(ByteReader& r, PlayerStateMsg& m) {
    m.ackedSnapshotTick = r.u32();
    m.state = readPlayerStateFull(r);
    return r.ok();
}

inline std::vector<uint8_t> encode(const Ping& m) {
    ByteWriter w(8);
    w.u8(static_cast<uint8_t>(MsgType::Ping)); w.u32(m.clientTimeMs);
    return w.take();
}
inline bool decode(ByteReader& r, Ping& m) { m.clientTimeMs = r.u32(); return r.ok(); }

inline std::vector<uint8_t> encode(const Pong& m) {
    ByteWriter w(16);
    w.u8(static_cast<uint8_t>(MsgType::Pong)); w.u32(m.clientTimeMs); w.u32(m.hostTimeMs); w.u32(m.hostTick);
    return w.take();
}
inline bool decode(ByteReader& r, Pong& m) {
    m.clientTimeMs = r.u32(); m.hostTimeMs = r.u32(); m.hostTick = r.u32();
    return r.ok();
}

inline std::vector<uint8_t> encode(const Chat& m) {
    ByteWriter w(64);
    w.u8(static_cast<uint8_t>(MsgType::Chat)); w.u8(m.fromId); w.str8(m.text.substr(0, kMaxChatBytes));
    return w.take();
}
inline bool decode(ByteReader& r, Chat& m) {
    m.fromId = r.u8(); m.text = r.str8();
    return r.ok();
}

inline std::vector<uint8_t> encode(const HostSettings& m) {
    ByteWriter w(8);
    w.u8(static_cast<uint8_t>(MsgType::HostSettings)); w.u32(m.hostFlags); w.u8(m.tickRate);
    return w.take();
}
inline bool decode(ByteReader& r, HostSettings& m) {
    m.hostFlags = r.u32(); m.tickRate = r.u8();
    return r.ok();
}

/// Peek the message type of a raw packet (Invalid if empty or out of range).
inline MsgType peekType(std::span<const uint8_t> pkt) {
    if (pkt.empty() || pkt[0] >= static_cast<uint8_t>(MsgType::Max_)) return MsgType::Invalid;
    return static_cast<MsgType>(pkt[0]);
}

/// Reader positioned after the type byte.
inline ByteReader bodyReader(std::span<const uint8_t> pkt) {
    return ByteReader(pkt.data() + 1, pkt.size() - 1);
}

/// Convenience: decode a typed message from a full packet (including type byte).
template <class T>
inline bool decodePacket(std::span<const uint8_t> pkt, T& out) {
    if (pkt.size() < 1) return false;
    ByteReader r = bodyReader(pkt);
    return decode(r, out);
}

}  // namespace dsproto
