// dscoop_testclient — exercises the whole network stack without the game.
// Fakes a Sam walking around and prints what it sees of the other players.
//
//   dscoop_testclient host  [--port 27015] [--relay relay.example.com[:27000]] [--nick Host] [--pvp]
//   dscoop_testclient join  --code ABC234 --relay relay.example.com[:27000] [--nick Guest]
//   dscoop_testclient join  --direct 192.168.1.10:27015 [--nick Guest]
//   common: --motion circle|walk|static  --speed 2.0  --duration 60  --quiet  --log-level debug
//          --chat "text"  (sent once connected)
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "dscommon/log.hpp"
#include "dscommon/version.hpp"
#include "dsnet/enet_transport.hpp"
#include "dsnet/session.hpp"

using namespace dsnet;
using namespace dsproto;

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

struct Args {
    std::string mode;  // host | join
    uint16_t port = 27015;
    std::string relay;
    std::string code;
    std::string direct;
    std::string nick;
    std::string motion = "circle";
    float speed = 2.0f;
    int duration = 0;  // seconds, 0 = until Ctrl-C
    bool quiet = false;
    bool pvp = false;
    std::string logLevel = "info";
    std::string chat;
    uint32_t tickRate = 30;
};

void usage() {
    std::puts(
        "dscoop_testclient v" DSCOOP_VERSION_STRING "\n"
        "  host [--port N] [--relay HOST[:PORT]] [--nick NAME] [--pvp] [--tick 30]\n"
        "  join --code CODE --relay HOST[:PORT] [--nick NAME]\n"
        "  join --direct HOST:PORT [--nick NAME]\n"
        "  common: --motion circle|walk|static --speed M/S --duration SEC --quiet --log-level LEVEL --chat TEXT");
}

bool parse(int argc, char** argv, Args& a) {
    if (argc < 2) return false;
    a.mode = argv[1];
    if (a.mode != "host" && a.mode != "join") return false;
    for (int i = 2; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&](std::string& out) { if (i + 1 >= argc) return false; out = argv[++i]; return true; };
        std::string v;
        if (k == "--port") { if (!next(v)) return false; a.port = static_cast<uint16_t>(std::stoi(v)); }
        else if (k == "--relay") { if (!next(a.relay)) return false; }
        else if (k == "--code") { if (!next(a.code)) return false; }
        else if (k == "--direct") { if (!next(a.direct)) return false; }
        else if (k == "--nick") { if (!next(a.nick)) return false; }
        else if (k == "--motion") { if (!next(a.motion)) return false; }
        else if (k == "--speed") { if (!next(v)) return false; a.speed = std::stof(v); }
        else if (k == "--duration") { if (!next(v)) return false; a.duration = std::stoi(v); }
        else if (k == "--tick") { if (!next(v)) return false; a.tickRate = static_cast<uint32_t>(std::stoi(v)); }
        else if (k == "--log-level") { if (!next(a.logLevel)) return false; }
        else if (k == "--chat") { if (!next(a.chat)) return false; }
        else if (k == "--quiet") a.quiet = true;
        else if (k == "--pvp") a.pvp = true;
        else if (k == "--help" || k == "-h") return false;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return false; }
    }
    if (a.mode == "join" && a.code.empty() && a.direct.empty()) { std::fprintf(stderr, "join needs --code+--relay or --direct\n"); return false; }
    if (a.mode == "join" && !a.code.empty() && a.relay.empty()) { std::fprintf(stderr, "--code needs --relay\n"); return false; }
    if (a.nick.empty()) a.nick = a.mode == "host" ? "Host" : "Guest";
    return true;
}

/// Fake Sam: a circle around the origin, a random-ish walk, or standing still.
PlayerState fakeState(const Args& a, double tSec, uint32_t frame, uint8_t seed) {
    PlayerState s;
    s.tick = frame;
    s.flags = PF_Fake | PF_Grounded;
    s.health = 1.0f; s.stamina = 0.8f;
    const float phase = static_cast<float>(seed) * 1.7f;
    if (a.motion == "static") {
        s.pos = {100.0f + seed * 2.0f, 200.0f, 50.0f};
    } else if (a.motion == "walk") {
        const float t = static_cast<float>(tSec);
        s.pos = {100.0f + a.speed * t + 3.0f * std::sin(t * 0.5f + phase), 200.0f + 5.0f * std::sin(t * 0.3f + phase), 50.0f};
        s.vel = {a.speed + 1.5f * std::cos(t * 0.5f + phase), 1.5f * std::cos(t * 0.3f + phase), 0};
        s.yaw = std::atan2(s.vel.y, s.vel.x);
        if (a.speed > 4.0f) s.flags |= PF_Sprinting;
    } else {  // circle
        const float r = 10.0f;
        const float w = a.speed / r;
        const float ang = static_cast<float>(tSec) * w + phase;
        s.pos = {100.0f + r * std::cos(ang), 200.0f + r * std::sin(ang), 50.0f};
        s.vel = {-r * w * std::sin(ang), r * w * std::cos(ang), 0};
        s.yaw = ang + 1.5708f;
    }
    return s;
}

void printTable(Session& s, uint32_t now, const Args& a) {
    const auto st = s.stats();
    std::printf("\n[%s] id=%u tick=%u rtt=%ums snap/s=%u state/s=%u desync=%u relayRtt=%ums in=%llu out=%llu lost=%llu\n",
                s.statusText().c_str(), s.localId(), st.hostTick, st.rttMs, st.snapshotsPerSec, st.statesPerSec, st.desyncs,
                st.relayRttMs, static_cast<unsigned long long>(st.transport.bytesReceived),
                static_cast<unsigned long long>(st.transport.bytesSent), static_cast<unsigned long long>(st.transport.packetsLost));
    if (s.role() == Session::Role::Host && !s.roomCode().empty()) std::printf("  ROOM CODE: %s\n", s.roomCode().c_str());
    for (const auto& [id, p] : s.players()) {
        auto st2 = s.remoteStateAt(id, now);
        if (st2) std::printf("  #%u %-16s pos=(%8.2f %8.2f %8.2f) yaw=%6.2f vel=%5.2f %s%s\n", id, p.nick.c_str(), st2->pos.x, st2->pos.y, st2->pos.z,
                             st2->yaw, st2->vel.length(), (st2->flags & PF_Fake) ? "[fake]" : "", (st2->flags & PF_Sprinting) ? "[sprint]" : "");
        else std::printf("  #%u %-16s (no state yet)\n", id, p.nick.c_str());
    }
    (void)a;
}
}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) { usage(); return 2; }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    dscommon::log::Options lo;
    lo.name = "testclient";
    lo.console = true;
    lo.level = dscommon::log::parseLevel(a.logLevel);
    dscommon::log::init(lo);

    auto session = std::make_unique<Session>(std::make_unique<EnetTransport>());
    session->onLog = [](const std::string& s) { DSLOG_INFO("{}", s); };
    session->onPlayerJoined = [](const PlayerInfo& p) { DSLOG_INFO(">>> player {} '{}' joined", p.id, p.nick); };
    session->onPlayerLeft = [](uint8_t id, uint8_t r) { DSLOG_INFO("<<< player {} left (reason {})", id, r); };
    session->onChat = [](uint8_t from, const std::string& t) { DSLOG_INFO("[chat] #{}: {}", from, t); };

    SessionConfig cfg;
    cfg.nick = a.nick;
    cfg.clientVersion = DSCOOP_VERSION_INT;
    cfg.gameBuild = 0;
    cfg.features = CF_FakeClient;
    cfg.tickRate = a.tickRate;
    if (a.pvp) cfg.hostFlags |= HF_PvP;

    std::string err;
    std::optional<RelayOptions> relay;
    if (!a.relay.empty()) {
        auto ep = Endpoint::parse(a.relay, relay::kDefaultControlPort);
        if (!ep) { std::fprintf(stderr, "bad --relay\n"); return 2; }
        relay = RelayOptions{ep->host, ep->port};
    }
    bool ok = false;
    if (a.mode == "host") ok = session->host(cfg, a.port, relay, &err);
    else if (!a.direct.empty()) {
        auto ep = Endpoint::parse(a.direct, 27015);
        if (!ep) { std::fprintf(stderr, "bad --direct\n"); return 2; }
        ok = session->joinDirect(cfg, *ep, &err);
    } else ok = session->joinRelay(cfg, *relay, a.code, &err);
    if (!ok) { DSLOG_ERROR("start failed: {}", err); return 1; }

    const auto t0 = std::chrono::steady_clock::now();
    uint32_t frame = 0, lastPrint = 0;
    bool chatSent = false;
    const uint8_t seed = static_cast<uint8_t>(std::hash<std::string>{}(a.nick) & 0xFF);
    int exitCode = 0;
    while (!g_stop) {
        const uint32_t now = nowMs();
        const double tSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        session->setLocalState(fakeState(a, tSec, frame++, seed));
        session->update(now);
        if (session->state() == Session::State::Failed) { DSLOG_ERROR("session failed: {}", session->error()); exitCode = 1; break; }
        if (!a.chat.empty() && !chatSent && session->state() == Session::State::Connected) { session->sendChat(a.chat); chatSent = true; }
        if (!a.quiet && now - lastPrint >= 1000) { lastPrint = now; printTable(*session, now, a); std::fflush(stdout); }
        if (a.duration > 0 && tSec >= a.duration) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    printTable(*session, nowMs(), a);
    session->leave();
    dscommon::log::shutdown();
    return exitCode;
}
