#pragma once
// WorldSnapshot: host -> guests at tick rate. Delta-compressed against a baseline snapshot the
// guest acknowledged (PlayerStateMsg::ackedSnapshotTick). If the baseline is unknown to the
// encoder (too old) or the guest never acked, a full snapshot is sent (baselineTick = 0).
//
// Wire format (after the MsgType byte):
//   u32 hostTick
//   u32 baselineTick            0 = full
//   u8  count
//   count x { u8 id, u8 mask, [fields present in mask] }
//     mask bits: 1 Pos(3xf32) 2 Rot(u16 yaw, i16 pitch) 4 Vel(3xi16) 8 Anim(u16) 16 Flags(u16) 32 Vitals(u8,u8)
//   A player present in the baseline but absent in the snapshot has left (see PlayerLeft as well).
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "dsproto/messages.hpp"

namespace dsproto {

enum StateMask : uint8_t {
    SM_Pos = 1 << 0,
    SM_Rot = 1 << 1,
    SM_Vel = 1 << 2,
    SM_Anim = 1 << 3,
    SM_Flags = 1 << 4,
    SM_Vitals = 1 << 5,
    SM_All = SM_Pos | SM_Rot | SM_Vel | SM_Anim | SM_Flags | SM_Vitals,
};

struct SnapshotEntry {
    uint8_t id = kInvalidPlayerId;
    PlayerState state;  // tick field = snapshot hostTick
};

struct WorldSnapshot {
    uint32_t hostTick = 0;
    std::vector<SnapshotEntry> entries;  // sorted by id

    const PlayerState* find(uint8_t id) const {
        for (const auto& e : entries) if (e.id == id) return &e.state;
        return nullptr;
    }
};

namespace detail {
inline uint8_t diffMask(const PlayerState& a, const PlayerState& b) {
    uint8_t m = 0;
    if (a.pos != b.pos) m |= SM_Pos;
    if (a.yaw != b.yaw || a.pitch != b.pitch) m |= SM_Rot;
    if (a.vel != b.vel) m |= SM_Vel;
    if (a.animState != b.animState) m |= SM_Anim;
    if (a.flags != b.flags) m |= SM_Flags;
    if (a.health != b.health || a.stamina != b.stamina) m |= SM_Vitals;
    return m;
}
inline void writeFields(ByteWriter& w, const PlayerState& s, uint8_t mask) {
    if (mask & SM_Pos) { w.f32(s.pos.x); w.f32(s.pos.y); w.f32(s.pos.z); }
    if (mask & SM_Rot) { w.u16(quant::angleToU16(s.yaw)); w.i16(quant::toI16(s.pitch, 10000.0f)); }
    if (mask & SM_Vel) { w.i16(quant::toI16(s.vel.x, 256.0f)); w.i16(quant::toI16(s.vel.y, 256.0f)); w.i16(quant::toI16(s.vel.z, 256.0f)); }
    if (mask & SM_Anim) w.u16(s.animState);
    if (mask & SM_Flags) w.u16(s.flags);
    if (mask & SM_Vitals) { w.u8(quant::unitToU8(s.health)); w.u8(quant::unitToU8(s.stamina)); }
}
inline void readFields(ByteReader& r, PlayerState& s, uint8_t mask) {
    if (mask & SM_Pos) { s.pos.x = r.f32(); s.pos.y = r.f32(); s.pos.z = r.f32(); }
    if (mask & SM_Rot) { s.yaw = quant::u16ToAngle(r.u16()); s.pitch = quant::fromI16(r.i16(), 10000.0f); }
    if (mask & SM_Vel) { s.vel.x = quant::fromI16(r.i16(), 256.0f); s.vel.y = quant::fromI16(r.i16(), 256.0f); s.vel.z = quant::fromI16(r.i16(), 256.0f); }
    if (mask & SM_Anim) s.animState = r.u16();
    if (mask & SM_Flags) s.flags = r.u16();
    if (mask & SM_Vitals) { s.health = quant::u8ToUnit(r.u8()); s.stamina = quant::u8ToUnit(r.u8()); }
}
}  // namespace detail

/// Encode `snap` as a delta against `baseline` (nullptr = full). States must be quantized
/// (see quantize()) so that equality compares what the guest actually holds.
inline std::vector<uint8_t> encodeSnapshot(const WorldSnapshot& snap, const WorldSnapshot* baseline) {
    ByteWriter w(16 + snap.entries.size() * 34);
    w.u8(static_cast<uint8_t>(MsgType::WorldSnapshot));
    w.u32(snap.hostTick);
    w.u32(baseline ? baseline->hostTick : 0);
    w.u8(static_cast<uint8_t>(std::min<size_t>(snap.entries.size(), kMaxPlayers)));
    size_t n = 0;
    for (const auto& e : snap.entries) {
        if (n++ >= kMaxPlayers) break;
        uint8_t mask = SM_All;
        if (baseline) {
            if (const PlayerState* b = baseline->find(e.id)) mask = detail::diffMask(*b, e.state);
        }
        w.u8(e.id);
        w.u8(mask);
        detail::writeFields(w, e.state, mask);
    }
    return w.take();
}

enum class SnapshotDecodeResult { Ok, Malformed, UnknownBaseline };

/// Decode into `out`. `baselineLookup(tick)` must return the snapshot the guest holds for that
/// tick or nullptr. Entries missing from the delta keep baseline values; entries new in the
/// delta without a baseline must carry a full mask or decoding fails as Malformed.
template <class Lookup>
inline SnapshotDecodeResult decodeSnapshot(std::span<const uint8_t> pkt, WorldSnapshot& out, Lookup&& baselineLookup) {
    if (pkt.size() < 10) return SnapshotDecodeResult::Malformed;
    ByteReader r = bodyReader(pkt);
    out.hostTick = r.u32();
    const uint32_t baselineTick = r.u32();
    const uint8_t count = r.u8();
    if (count > kMaxPlayers) return SnapshotDecodeResult::Malformed;
    const WorldSnapshot* baseline = nullptr;
    if (baselineTick != 0) {
        baseline = baselineLookup(baselineTick);
        if (!baseline) return SnapshotDecodeResult::UnknownBaseline;
    }
    out.entries.clear();
    for (uint8_t i = 0; i < count; ++i) {
        SnapshotEntry e;
        e.id = r.u8();
        const uint8_t mask = r.u8();
        if (baseline) {
            if (const PlayerState* b = baseline->find(e.id)) e.state = *b;
            else if (mask != SM_All) return SnapshotDecodeResult::Malformed;
        } else if (mask != SM_All) {
            return SnapshotDecodeResult::Malformed;
        }
        detail::readFields(r, e.state, mask);
        e.state.tick = out.hostTick;
        if (r.failed()) return SnapshotDecodeResult::Malformed;
        out.entries.push_back(std::move(e));
    }
    std::sort(out.entries.begin(), out.entries.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return r.ok() ? SnapshotDecodeResult::Ok : SnapshotDecodeResult::Malformed;
}

/// Ring of recent snapshots by tick (host keeps what it sent; guest keeps what it decoded).
class SnapshotHistory {
public:
    explicit SnapshotHistory(size_t capacity = 64) : capacity_(capacity) {}

    void push(WorldSnapshot snap) {
        ring_.push_back(std::move(snap));
        while (ring_.size() > capacity_) ring_.pop_front();
    }
    const WorldSnapshot* find(uint32_t tick) const {
        for (auto it = ring_.rbegin(); it != ring_.rend(); ++it)
            if (it->hostTick == tick) return &*it;
        return nullptr;
    }
    const WorldSnapshot* latest() const { return ring_.empty() ? nullptr : &ring_.back(); }
    size_t size() const { return ring_.size(); }
    void clear() { ring_.clear(); }

private:
    size_t capacity_;
    std::deque<WorldSnapshot> ring_;
};

}  // namespace dsproto
