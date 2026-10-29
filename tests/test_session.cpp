#include <memory>

#include "dsnet/loopback_transport.hpp"
#include "dsnet/session.hpp"
#include "minitest.hpp"

using namespace dsnet;
using namespace dsproto;

namespace {
struct World {
    std::shared_ptr<LoopbackNetwork> net = std::make_shared<LoopbackNetwork>();
    uint32_t now = 1000;
    std::vector<Session*> sessions;
    void step(uint32_t ms = 10) {
        now += ms;
        for (auto* s : sessions) s->update(now);
    }
    void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 10) step(10); }
    std::unique_ptr<Session> make() {
        auto s = std::make_unique<Session>(std::make_unique<LoopbackTransport>(net));
        sessions.push_back(s.get());
        return s;
    }
};
SessionConfig cfg(const char* nick) {
    SessionConfig c; c.nick = nick; c.clientVersion = 0x000100; c.tickRate = 30; return c;
}
}  // namespace

TEST(host_and_two_guests_exchange_positions) {
    World w;
    auto host = w.make(); auto g1 = w.make(); auto g2 = w.make();
    std::string err;
    CHECK(host->host(cfg("Host"), 27015, std::nullopt, &err));
    CHECK(host->state() == Session::State::Connected);
    CHECK(g1->joinDirect(cfg("Fragile"), Endpoint{"loop", 27015}, &err));
    CHECK(g2->joinDirect(cfg("Deadman"), Endpoint{"loop", 27015}, &err));
    w.run(200);
    CHECK(g1->state() == Session::State::Connected);
    CHECK(g2->state() == Session::State::Connected);
    CHECK_EQ(host->players().size(), size_t(2));
    CHECK_EQ(g1->players().size(), size_t(2));  // host + other guest
    CHECK_EQ(g1->localId(), 1); CHECK_EQ(g2->localId(), 2);

    host->setLocalState(PlayerState{.pos = {10, 20, 30}});
    g1->setLocalState(PlayerState{.pos = {1, 1, 1}, .vel = {1, 0, 0}});
    g2->setLocalState(PlayerState{.pos = {2, 2, 2}});
    w.run(1500);  // > 1 s so the per-second stats window closes at least once
    // Host sees guests.
    CHECK(host->players().at(1).hasState);
    CHECK(host->players().at(1).last.pos == Vec3{1, 1, 1});
    CHECK(host->players().at(2).last.pos == Vec3{2, 2, 2});
    // Guests see host and each other via snapshots.
    CHECK(g1->players().at(0).hasState);
    CHECK(g1->players().at(0).last.pos == Vec3{10, 20, 30});
    CHECK(g1->players().at(2).last.pos == Vec3{2, 2, 2});
    CHECK(g2->players().at(1).last.pos == Vec3{1, 1, 1});
    auto s = g2->remoteStateAt(0, w.now);
    CHECK(s.has_value());
    CHECK(g1->stats().snapshotsPerSec >= 25);
    CHECK_EQ(g1->stats().desyncs, 0u);

    // Guest leaves -> everybody notified.
    bool leftSeen = false;
    g2->onPlayerLeft = [&](uint8_t id, uint8_t) { leftSeen = id == 1; };
    g1->leave();
    w.sessions.erase(std::find(w.sessions.begin(), w.sessions.end(), g1.get()));
    w.run(100);
    CHECK_EQ(host->players().size(), size_t(1));
    CHECK(leftSeen);
}

TEST(rejects_version_mismatch_and_full_session) {
    World w;
    auto host = w.make(); auto bad = w.make();
    std::string err;
    SessionConfig hc = cfg("Host"); hc.maxPlayers = 2;  // host + 1
    CHECK(host->host(hc, 27016, std::nullopt, &err));
    SessionConfig bc = cfg("Old"); bc.clientVersion = 0x000099;
    CHECK(bad->joinDirect(bc, Endpoint{"loop", 27016}, &err));
    w.run(100);
    CHECK(bad->state() == Session::State::Failed);
    CHECK(bad->error().find("rejected") != std::string::npos);

    auto ok = w.make(); auto extra = w.make();
    CHECK(ok->joinDirect(cfg("Ok"), Endpoint{"loop", 27016}, &err));
    w.run(100);
    CHECK(ok->state() == Session::State::Connected);
    // Loopback has exactly maxPeers slots; a 2nd connection cannot even be created.
    CHECK(!extra->joinDirect(cfg("Extra"), Endpoint{"loop", 27016}, &err));
}

TEST(lossy_link_recovers_with_full_snapshots) {
    World w;
    w.net->lossRate = 0.3f;
    w.net->latencyMs = 40;
    auto host = w.make(); auto g = w.make();
    std::string err;
    CHECK(host->host(cfg("Host"), 27017, std::nullopt, &err));
    CHECK(g->joinDirect(cfg("G"), Endpoint{"loop", 27017}, &err));
    w.run(500);
    CHECK(g->state() == Session::State::Connected);
    for (int i = 0; i < 100; ++i) {
        host->setLocalState(PlayerState{.pos = {static_cast<float>(i), 0, 0}});
        w.step(10);
    }
    w.run(200);
    CHECK(g->players().at(0).hasState);
    CHECK(g->players().at(0).last.pos.x > 90.0f);  // converged despite 30% loss
    auto interp = g->remoteStateAt(0, w.now);
    CHECK(interp.has_value());
}

TEST(no_hello_gets_dropped) {
    World w;
    auto host = w.make();
    std::string err;
    SessionConfig hc = cfg("Host"); hc.helloTimeoutMs = 100;
    CHECK(host->host(hc, 27018, std::nullopt, &err));
    LoopbackTransport rogue(w.net);
    CHECK(rogue.open(3, &err));
    PeerId p = rogue.connect(Endpoint{"loop", 27018}, 3, &err);
    CHECK(p != kInvalidPeer);
    std::vector<Event> evs;
    bool disconnected = false;
    for (int i = 0; i < 30; ++i) {
        w.step(10);
        evs.clear();
        rogue.service(w.now, evs);
        for (auto& e : evs) if (e.type == EventType::Disconnected) disconnected = true;
    }
    CHECK(disconnected);
}

int main() { return RUN_ALL_TESTS(); }
