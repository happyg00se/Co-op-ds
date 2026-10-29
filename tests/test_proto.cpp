#include "dsproto/interp.hpp"
#include "dsproto/messages.hpp"
#include "dsproto/relay_control.hpp"
#include "dsproto/snapshot.hpp"
#include "minitest.hpp"

using namespace dsproto;

TEST(byteio_roundtrip) {
    ByteWriter w;
    w.u8(0xAB); w.u16(0x1234); w.u32(0xDEADBEEF); w.u64(0x0102030405060708ULL); w.f32(3.5f); w.str8("hi"); w.str16("hello");
    ByteReader r(w.data());
    CHECK_EQ(r.u8(), 0xAB); CHECK_EQ(r.u16(), 0x1234); CHECK_EQ(r.u32(), 0xDEADBEEFu);
    CHECK_EQ(r.u64(), 0x0102030405060708ULL); CHECK_EQ(r.f32(), 3.5f);
    CHECK_EQ(r.str8(), std::string("hi")); CHECK_EQ(r.str16(), std::string("hello"));
    CHECK(r.ok());
    // Over-read fails safely.
    CHECK_EQ(r.u32(), 0u);
    CHECK(r.failed());
}

TEST(quant_angles) {
    for (float a : {-3.1f, -1.0f, 0.0f, 0.5f, 2.0f, 3.1f}) {
        float b = quant::u16ToAngle(quant::angleToU16(a));
        CHECK_NEAR(a, b, 0.0002);
    }
    CHECK_NEAR(quant::lerpAngle(3.0f, -3.0f, 0.5f), -3.14159f, 0.01);  // shortest arc across the seam
}

TEST(hello_welcome_roundtrip) {
    Hello h; h.clientVersion = 0x010203; h.gameBuild = 42; h.features = CF_FakeClient; h.nick = "Sam Porter Bridges";
    auto pkt = encode(h);
    CHECK(peekType(pkt) == MsgType::Hello);
    Hello d; CHECK(decodePacket(pkt, d));
    CHECK_EQ(d.clientVersion, h.clientVersion); CHECK_EQ(d.nick, h.nick); CHECK_EQ(d.features, h.features);

    Welcome w; w.playerId = 3; w.hostTick = 1000; w.sessionId = 0xABCDEF; w.hostFlags = HF_PvP;
    w.players = {{0, "Host"}, {1, "Fragile"}};
    auto wp = encode(w);
    Welcome wd; CHECK(decodePacket(wp, wd));
    CHECK_EQ(wd.playerId, 3); CHECK_EQ(wd.players.size(), size_t(2)); CHECK_EQ(wd.players[1].nick, std::string("Fragile"));
}

TEST(player_state_quantization) {
    PlayerState s; s.tick = 7; s.pos = {123.456f, -78.9f, 1000.25f}; s.yaw = 1.0f; s.pitch = -0.25f; s.vel = {1.5f, -2.25f, 0.125f};
    s.animState = 77; s.flags = PF_Grounded | PF_Sprinting; s.health = 0.5f; s.stamina = 0.25f;
    PlayerStateMsg m; m.ackedSnapshotTick = 99; m.state = s;
    auto pkt = encode(m);
    CHECK_EQ(pkt.size(), size_t(1 + 4 + 32));
    PlayerStateMsg d; CHECK(decodePacket(pkt, d));
    CHECK_EQ(d.ackedSnapshotTick, 99u);
    CHECK(d.state.pos == s.pos);
    CHECK_NEAR(d.state.yaw, s.yaw, 0.0002); CHECK_NEAR(d.state.pitch, s.pitch, 0.0002);
    CHECK_NEAR(d.state.vel.y, s.vel.y, 0.004);
    CHECK_EQ(d.state.flags, s.flags); CHECK_EQ(d.state.animState, 77);
    CHECK(quantize(d.state) == d.state);  // idempotent
}

TEST(snapshot_full_and_delta) {
    WorldSnapshot base; base.hostTick = 100;
    base.entries.push_back({0, quantize(PlayerState{.tick = 100, .pos = {1, 2, 3}, .yaw = 0.1f})});
    base.entries.push_back({1, quantize(PlayerState{.tick = 100, .pos = {4, 5, 6}, .yaw = 0.2f})});
    auto fullPkt = encodeSnapshot(base, nullptr);

    SnapshotHistory hist;
    WorldSnapshot decoded;
    auto res = decodeSnapshot(fullPkt, decoded, [&](uint32_t t) { return hist.find(t); });
    CHECK(res == SnapshotDecodeResult::Ok);
    CHECK_EQ(decoded.entries.size(), size_t(2));
    CHECK(decoded.find(1)->pos == Vec3{4, 5, 6});
    hist.push(decoded);

    WorldSnapshot next = base; next.hostTick = 101;
    next.entries[1].state.pos = {4.5f, 5, 6};  // only player 1 moved
    next.entries[1].state.tick = 101;
    auto deltaPkt = encodeSnapshot(next, &base);
    CHECK(deltaPkt.size() < fullPkt.size());
    // player 0 unchanged -> only id+mask (2 bytes); player 1 -> 2 + 12 bytes pos
    CHECK_EQ(deltaPkt.size(), size_t(1 + 4 + 4 + 1 + 2 + 2 + 12));

    WorldSnapshot d2;
    res = decodeSnapshot(deltaPkt, d2, [&](uint32_t t) { return hist.find(t); });
    CHECK(res == SnapshotDecodeResult::Ok);
    CHECK(d2.find(0)->pos == Vec3{1, 2, 3});
    CHECK(d2.find(1)->pos == Vec3{4.5f, 5, 6});
    CHECK_EQ(d2.find(1)->tick, 101u);

    // Unknown baseline must be reported, not guessed.
    WorldSnapshot d3;
    res = decodeSnapshot(deltaPkt, d3, [&](uint32_t) -> const WorldSnapshot* { return nullptr; });
    CHECK(res == SnapshotDecodeResult::UnknownBaseline);

    // New player appearing in a delta carries a full mask.
    WorldSnapshot withNew = next; withNew.hostTick = 102;
    withNew.entries.push_back({2, quantize(PlayerState{.tick = 102, .pos = {9, 9, 9}})});
    auto p3 = encodeSnapshot(withNew, &next);
    hist.push(d2);
    WorldSnapshot d4;
    res = decodeSnapshot(p3, d4, [&](uint32_t t) { return hist.find(t); });
    CHECK(res == SnapshotDecodeResult::Ok);
    CHECK_EQ(d4.entries.size(), size_t(3));
    CHECK(d4.find(2)->pos == Vec3{9, 9, 9});
}

TEST(snapshot_rejects_garbage) {
    std::vector<uint8_t> junk = {static_cast<uint8_t>(MsgType::WorldSnapshot), 1, 0, 0, 0, 0, 0, 0, 0, 200};
    WorldSnapshot d;
    CHECK(decodeSnapshot(junk, d, [](uint32_t) -> const WorldSnapshot* { return nullptr; }) == SnapshotDecodeResult::Malformed);
}

TEST(interpolator_blends_and_extrapolates) {
    PlayerInterpolator ip;
    ip.push(PlayerState{.tick = 10, .pos = {0, 0, 0}, .vel = {30, 0, 0}});
    ip.push(PlayerState{.tick = 11, .pos = {1, 0, 0}, .vel = {30, 0, 0}});
    ip.push(PlayerState{.tick = 12, .pos = {2, 0, 0}, .vel = {30, 0, 0}});
    // interpDelay 3 ticks: at hostTick 14.5 we sample t=11.5 -> x = 1.5
    auto s = ip.sample(14.5);
    CHECK(s.has_value()); CHECK_NEAR(s->pos.x, 1.5f, 1e-4);
    // beyond last sample: extrapolate 1 tick (1/30 s * 30 m/s = 1 m)
    s = ip.sample(16.0);
    CHECK_NEAR(s->pos.x, 3.0f, 1e-3);
    // clamp extrapolation at 8 ticks
    s = ip.sample(100.0);
    CHECK_NEAR(s->pos.x, 2.0f + 8.0f, 1e-3);
}

TEST(tick_clock) {
    TickClock c; c.setTickRate(30);
    c.onPong(3000, 10000, 60);  // host tick 3000 at local 10 s, rtt 60 ms -> +0.9 tick
    CHECK(c.synced());
    CHECK_NEAR(c.hostTickAt(10000), 3000.9, 0.01);
    CHECK_NEAR(c.hostTickAt(11000), 3030.9, 0.01);
}

TEST(relay_control_roundtrip) {
    using namespace dsproto::relay;
    auto cr = encodeCreate(7, 0x010000);
    CHECK(peekOp(cr) == Op::Create);
    auto j = encodeJoin("ABC234", 1);
    CHECK(peekOp(j) == Op::Join);
    CHECK_EQ(j.size(), size_t(6 + 6 + 4));
    // Build a CREATED like the relay does and parse it.
    ByteWriter w; w.bytes(kMagic); w.u8(kVersion); w.u8(static_cast<uint8_t>(Op::Created));
    for (char ch : std::string("ABC234")) w.u8(static_cast<uint8_t>(ch));
    w.u64(0x1122334455667788ULL); w.u16(27101); w.u8(2); w.u16(27102); w.u16(27103);
    Created c; CHECK(decodeCreated(w.data(), c));
    CHECK_EQ(c.code, std::string("ABC234")); CHECK_EQ(c.hostPort, 27101); CHECK_EQ(c.guestPorts.size(), size_t(2));
    CHECK(isValidRoomCode("ABC234")); CHECK(!isValidRoomCode("ABC0O1")); CHECK(!isValidRoomCode("ABC"));
    CHECK_EQ(normalizeRoomCode("abc-234"), std::string("ABC234"));
    // ENet-looking data is not a control packet.
    std::vector<uint8_t> enetish = {0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00};
    CHECK(!isControlPacket(enetish));
}

int main() { return RUN_ALL_TESTS(); }
