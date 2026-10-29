#include "dsnet/session.hpp"

#include <algorithm>
#include <random>

namespace dsnet {
using namespace dsproto;

namespace {
uint64_t randomSessionId() {
    std::random_device rd;
    return (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ static_cast<uint64_t>(nowMs());
}
const char* stateName(Session::State s) {
    switch (s) {
        case Session::State::Idle: return "idle";
        case Session::State::RelayHandshake: return "relay handshake";
        case Session::State::Connecting: return "connecting";
        case Session::State::Handshaking: return "handshaking";
        case Session::State::Connected: return "connected";
        case Session::State::Failed: return "failed";
        case Session::State::Closed: return "closed";
    }
    return "?";
}
}  // namespace

Session::Session(std::unique_ptr<ITransport> transport) : transport_(std::move(transport)) {
    relay_.onLog = [this](const std::string& s) { log(s); };
    transport_->setRawHandler([this](const Endpoint& from, std::span<const uint8_t> d) {
        return relay_.handleRaw(from, d, nowMs());
    });
}

Session::~Session() { leave(); }

void Session::setState(State s) {
    if (state_ == s) return;
    state_ = s;
    log(std::string("session: ") + stateName(s));
    if (onStateChanged) onStateChanged(s);
}

void Session::fail(const std::string& why) {
    error_ = why;
    log("session failed: " + why);
    setState(State::Failed);
}

// ---------------------------------------------------------------------------
bool Session::host(const SessionConfig& cfg, uint16_t listenPort, const std::optional<RelayOptions>& relay, std::string* err) {
    leave();
    cfg_ = cfg;
    role_ = Role::Host;
    localId_ = 0;
    sessionId_ = randomSessionId();
    hostTick_ = 1;
    players_.clear();
    pending_.clear();
    sentHistory_.clear();
    error_.clear();
    const size_t maxPeers = cfg_.maxPlayers > 1 ? cfg_.maxPlayers - 1 : 1;
    if (!transport_->listen(listenPort, maxPeers, kChannelCount, err)) { fail(err ? *err : "listen failed"); return false; }
    log("host: listening on UDP " + std::to_string(transport_->localPort()) + " for " + std::to_string(maxPeers) + " guests");
    useRelay_ = relay.has_value();
    if (useRelay_) {
        relayOpts_ = *relay;
        RelayClient::Options ro;
        ro.relay = Endpoint{relayOpts_.host, relayOpts_.controlPort};
        ro.clientVersion = cfg_.clientVersion;
        ro.maxGuests = static_cast<uint8_t>(maxPeers);
        relay_.beginCreate(*transport_, ro, nowMs());
        setState(State::RelayHandshake);
    } else {
        setState(State::Connected);
    }
    lastTickMs_ = nowMs();
    return true;
}

bool Session::joinDirect(const SessionConfig& cfg, const Endpoint& hostEp, std::string* err) {
    leave();
    cfg_ = cfg;
    role_ = Role::Guest;
    useRelay_ = false;
    players_.clear();
    recvHistory_.clear();
    error_.clear();
    ackedSnapshotTick_ = 0;
    clock_ = TickClock{};
    if (!transport_->open(kChannelCount, err)) { fail(err ? *err : "open failed"); return false; }
    hostPeer_ = transport_->connect(hostEp, kChannelCount, err);
    if (hostPeer_ == kInvalidPeer) { fail(err ? *err : "connect failed"); return false; }
    connectStartedMs_ = nowMs();
    log("guest: connecting to " + hostEp.toString());
    setState(State::Connecting);
    return true;
}

bool Session::joinRelay(const SessionConfig& cfg, const RelayOptions& relay, const std::string& roomCode, std::string* err) {
    leave();
    cfg_ = cfg;
    role_ = Role::Guest;
    useRelay_ = true;
    relayOpts_ = relay;
    players_.clear();
    recvHistory_.clear();
    error_.clear();
    ackedSnapshotTick_ = 0;
    clock_ = TickClock{};
    if (!transport_->open(kChannelCount, err)) { fail(err ? *err : "open failed"); return false; }
    RelayClient::Options ro;
    ro.relay = Endpoint{relayOpts_.host, relayOpts_.controlPort};
    ro.clientVersion = cfg_.clientVersion;
    relay_.beginJoin(*transport_, ro, roomCode, nowMs());
    if (relay_.phase() == RelayClient::Phase::Failed) {
        if (err) *err = relay_.error();
        fail(relay_.error());
        return false;
    }
    setState(State::RelayHandshake);
    return true;
}

void Session::leave() {
    if (role_ == Role::None) return;
    const uint32_t now = nowMs();
    if (role_ == Role::Host) {
        PlayerLeft pl; pl.id = 0; pl.reason = 2;
        for (auto& [id, p] : players_) if (p.peer != kInvalidPeer) transport_->disconnect(p.peer, static_cast<uint32_t>(RejectReason::HostShutdown));
    } else if (hostPeer_ != kInvalidPeer) {
        transport_->disconnect(hostPeer_, 0);
    }
    relay_.stop(now);
    transport_->flush();
    transport_->close();
    players_.clear();
    pending_.clear();
    hostPeer_ = kInvalidPeer;
    role_ = Role::None;
    localId_ = kInvalidPlayerId;
    setState(State::Closed);
}

// ---------------------------------------------------------------------------
void Session::update(uint32_t nowMs) {
    if (role_ == Role::None) return;

    if (useRelay_) {
        relay_.update(nowMs);
        if (relay_.phase() == RelayClient::Phase::Failed && state_ != State::Failed) {
            fail("relay: " + relay_.error());
            return;
        }
        if (state_ == State::RelayHandshake) {
            if (role_ == Role::Host && relay_.phase() == RelayClient::Phase::Active) {
                setState(State::Connected);
            } else if (role_ == Role::Guest && relay_.readyToConnect()) {
                std::string err;
                hostPeer_ = transport_->connect(*relay_.hostEndpoint(), kChannelCount, &err);
                if (hostPeer_ == kInvalidPeer) { fail(err); return; }
                connectStartedMs_ = nowMs;
                log("guest: connecting through relay " + relay_.hostEndpoint()->toString());
                setState(State::Connecting);
            }
        }
    }

    std::vector<Event> events;
    transport_->service(nowMs, events);
    for (const Event& ev : events) {
        if (role_ == Role::Host) hostHandleEvent(ev, nowMs);
        else guestHandleEvent(ev, nowMs);
        if (state_ == State::Failed) return;
    }

    if (role_ == Role::Host) {
        // Peers that never said Hello.
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (nowMs - it->second.connectedAtMs > cfg_.helloTimeoutMs) {
                log("host: peer " + std::to_string(it->first) + " never sent Hello, dropping");
                transport_->disconnect(it->first, static_cast<uint32_t>(RejectReason::Unknown));
                it = pending_.erase(it);
            } else ++it;
        }
        // Idle players.
        std::vector<uint8_t> dead;
        for (auto& [id, p] : players_) if (p.lastSeenMs && nowMs - p.lastSeenMs > cfg_.peerTimeoutMs) dead.push_back(id);
        for (uint8_t id : dead) hostDropPlayer(id, 1, nowMs);

        const uint32_t tickMs = 1000 / std::max<uint32_t>(1, cfg_.tickRate);
        if (nowMs - lastTickMs_ >= tickMs) {
            lastTickMs_ = nowMs;
            hostTickOnce(nowMs);
        }
    } else {
        if (state_ == State::Connecting && nowMs - connectStartedMs_ > cfg_.peerTimeoutMs) { fail("connect timeout"); return; }
        if (state_ == State::Handshaking && nowMs - connectStartedMs_ > cfg_.peerTimeoutMs) { fail("handshake timeout"); return; }
        if (state_ == State::Connected) {
            if (lastFromHostMs_ && nowMs - lastFromHostMs_ > cfg_.peerTimeoutMs) { fail("host timeout"); return; }
            const uint32_t stateMs = 1000 / std::max<uint32_t>(1, cfg_.tickRate);
            if (nowMs - lastStateSentMs_ >= stateMs) guestSendState(nowMs);
            if (nowMs - lastPingMs_ >= cfg_.pingIntervalMs) {
                lastPingMs_ = nowMs;
                Ping p; p.clientTimeMs = nowMs;
                transport_->send(hostPeer_, kChannelControl, encode(p), true);
            }
        }
    }

    if (nowMs - statWindowStartMs_ >= 1000) {
        statWindowStartMs_ = nowMs;
        snapshotsPerSec_ = statSnapshots_; statesPerSec_ = statStates_;
        statSnapshots_ = statStates_ = 0;
    }
    transport_->flush();
}

// ---------------------------------------------------------------------------
// Host
uint8_t Session::allocPlayerId() const {
    for (uint8_t id = 1; id < cfg_.maxPlayers; ++id) if (!players_.count(id)) return id;
    return kInvalidPlayerId;
}

RemotePlayer* Session::playerByPeer(PeerId peer) {
    for (auto& [id, p] : players_) if (p.peer == peer) return &p;
    return nullptr;
}

void Session::hostBroadcast(const std::vector<uint8_t>& pkt, uint8_t channel, bool reliable, PeerId except) {
    for (auto& [id, p] : players_) if (p.peer != kInvalidPeer && p.peer != except) transport_->send(p.peer, channel, pkt, reliable);
}

void Session::hostHandleEvent(const Event& ev, uint32_t nowMs) {
    switch (ev.type) {
        case EventType::Connected:
            pending_[ev.peer] = PendingPeer{nowMs};
            log("host: peer " + std::to_string(ev.peer) + " connected, waiting for Hello");
            break;
        case EventType::Disconnected:
            pending_.erase(ev.peer);
            if (RemotePlayer* p = playerByPeer(ev.peer)) hostDropPlayer(p->id, 0, nowMs);
            break;
        case EventType::Data: {
            const MsgType t = peekType(ev.data);
            if (pending_.count(ev.peer)) {
                if (t == MsgType::Hello) hostHandleHello(ev.peer, ev.data, nowMs);
                return;  // anything else before Hello is ignored
            }
            RemotePlayer* p = playerByPeer(ev.peer);
            if (!p) return;
            p->lastSeenMs = nowMs;
            switch (t) {
                case MsgType::PlayerState: {
                    PlayerStateMsg m;
                    if (!decodePacket(ev.data, m)) return;
                    p->ackedSnapshotTick = m.ackedSnapshotTick;
                    p->last = quantize(m.state);
                    p->last.tick = hostTick_;
                    p->hasState = true;
                    p->interp.push(p->last);
                    p->rttMs = transport_->roundTripMs(ev.peer);
                    statStates_++;
                    break;
                }
                case MsgType::Ping: {
                    Ping ping;
                    if (!decodePacket(ev.data, ping)) return;
                    Pong pong; pong.clientTimeMs = ping.clientTimeMs; pong.hostTimeMs = nowMs; pong.hostTick = hostTick_;
                    transport_->send(ev.peer, kChannelControl, encode(pong), true);
                    break;
                }
                case MsgType::Chat: {
                    Chat c;
                    if (!decodePacket(ev.data, c)) return;
                    c.fromId = p->id;  // never trust the sender's id
                    if (onChat) onChat(c.fromId, c.text);
                    hostBroadcast(encode(c), kChannelControl, true, kInvalidPeer);
                    break;
                }
                default: break;
            }
            break;
        }
    }
}

void Session::hostHandleHello(PeerId peer, std::span<const uint8_t> pkt, uint32_t nowMs) {
    Hello h;
    auto reject = [&](RejectReason r, const std::string& text) {
        Reject rj; rj.reason = r; rj.text = text;
        transport_->send(peer, kChannelControl, encode(rj), true);
        transport_->flush();
        transport_->disconnect(peer, static_cast<uint32_t>(r));
        pending_.erase(peer);
        log("host: rejected peer " + std::to_string(peer) + ": " + text);
    };
    if (!decodePacket(pkt, h)) { reject(RejectReason::Unknown, "malformed Hello"); return; }
    if (h.protocolVersion != kProtocolVersion) { reject(RejectReason::ProtocolMismatch, "protocol " + std::to_string(h.protocolVersion) + " != " + std::to_string(kProtocolVersion)); return; }
    if (cfg_.strictVersion && h.clientVersion != cfg_.clientVersion) { reject(RejectReason::ClientVersionMismatch, "DSCoop version differs"); return; }
    if (h.gameBuild && cfg_.gameBuild && h.gameBuild != cfg_.gameBuild) { reject(RejectReason::GameBuildMismatch, "game build differs"); return; }
    const uint8_t id = allocPlayerId();
    if (id == kInvalidPlayerId) { reject(RejectReason::ServerFull, "session full"); return; }
    std::string nick = h.nick.substr(0, kMaxNickBytes);
    if (nick.empty()) nick = "Porter" + std::to_string(id);

    pending_.erase(peer);
    RemotePlayer rp; rp.id = id; rp.nick = nick; rp.peer = peer; rp.lastSeenMs = nowMs;
    rp.interp.setTickRateHint(cfg_.tickRate);
    players_[id] = std::move(rp);

    Welcome w; w.playerId = id; w.hostTick = hostTick_; w.tickRate = static_cast<uint8_t>(cfg_.tickRate);
    w.sessionId = sessionId_; w.hostFlags = cfg_.hostFlags; w.hostVersion = cfg_.clientVersion;
    w.players.push_back({0, cfg_.nick});
    for (auto& [pid, p] : players_) if (pid != id) w.players.push_back({pid, p.nick});
    transport_->send(peer, kChannelControl, encode(w), true);

    PlayerJoined pj; pj.player = {id, nick};
    hostBroadcast(encode(pj), kChannelControl, true, peer);
    log("host: player " + std::to_string(id) + " '" + nick + "' joined (peer " + std::to_string(peer) + ")");
    if (onPlayerJoined) onPlayerJoined(pj.player);
}

void Session::hostDropPlayer(uint8_t id, uint8_t reason, uint32_t) {
    auto it = players_.find(id);
    if (it == players_.end()) return;
    const PeerId peer = it->second.peer;
    const std::string nick = it->second.nick;
    players_.erase(it);
    if (peer != kInvalidPeer && reason != 0) transport_->disconnect(peer, reason);
    PlayerLeft pl; pl.id = id; pl.reason = reason;
    hostBroadcast(encode(pl), kChannelControl, true, kInvalidPeer);
    log("host: player " + std::to_string(id) + " '" + nick + "' left (reason " + std::to_string(reason) + ")");
    if (onPlayerLeft) onPlayerLeft(id, reason);
}

void Session::hostTickOnce(uint32_t) {
    hostTick_++;
    WorldSnapshot snap;
    snap.hostTick = hostTick_;
    if (hasLocalState_) {
        PlayerState s = quantize(localState_);
        s.tick = hostTick_;
        snap.entries.push_back({0, s});
    }
    for (auto& [id, p] : players_) {
        if (!p.hasState) continue;
        PlayerState s = p.last;
        s.tick = hostTick_;
        snap.entries.push_back({id, s});
    }
    for (auto& [id, p] : players_) {
        if (p.peer == kInvalidPeer) continue;
        const WorldSnapshot* baseline = p.ackedSnapshotTick ? sentHistory_.find(p.ackedSnapshotTick) : nullptr;
        transport_->send(p.peer, kChannelState, encodeSnapshot(snap, baseline), false);
    }
    sentHistory_.push(std::move(snap));
    statSnapshots_++;
}

// ---------------------------------------------------------------------------
// Guest
void Session::guestHandleEvent(const Event& ev, uint32_t nowMs) {
    if (ev.peer != hostPeer_) return;
    switch (ev.type) {
        case EventType::Connected: {
            Hello h; h.clientVersion = cfg_.clientVersion; h.gameBuild = cfg_.gameBuild; h.features = cfg_.features; h.nick = cfg_.nick;
            transport_->send(hostPeer_, kChannelControl, encode(h), true);
            lastFromHostMs_ = nowMs;
            setState(State::Handshaking);
            break;
        }
        case EventType::Disconnected:
            if (state_ == State::Connected || state_ == State::Handshaking) fail(error_.empty() ? "disconnected by host (code " + std::to_string(ev.data32) + ")" : error_);
            else if (state_ == State::Connecting) fail("host unreachable");
            break;
        case EventType::Data: {
            lastFromHostMs_ = nowMs;
            switch (peekType(ev.data)) {
                case MsgType::Welcome: {
                    Welcome w;
                    if (!decodePacket(ev.data, w)) { fail("malformed Welcome"); return; }
                    localId_ = w.playerId; sessionId_ = w.sessionId; hostTick_ = w.hostTick;
                    cfg_.tickRate = w.tickRate ? w.tickRate : kDefaultTickRate;
                    cfg_.hostFlags = w.hostFlags;
                    clock_.setTickRate(cfg_.tickRate);
                    players_.clear();
                    for (const auto& pi : w.players) {
                        RemotePlayer rp; rp.id = pi.id; rp.nick = pi.nick; rp.lastSeenMs = nowMs;
                        rp.interp.setTickRateHint(cfg_.tickRate);
                        players_[pi.id] = std::move(rp);
                    }
                    log("guest: welcome, my id " + std::to_string(localId_) + ", " + std::to_string(w.players.size()) + " players, tick " + std::to_string(cfg_.tickRate) + " Hz");
                    setState(State::Connected);
                    break;
                }
                case MsgType::Reject: {
                    Reject r;
                    decodePacket(ev.data, r);
                    error_ = "rejected: " + r.text;
                    fail(error_);
                    break;
                }
                case MsgType::PlayerJoined: {
                    PlayerJoined pj;
                    if (!decodePacket(ev.data, pj)) return;
                    RemotePlayer rp; rp.id = pj.player.id; rp.nick = pj.player.nick; rp.lastSeenMs = nowMs;
                    rp.interp.setTickRateHint(cfg_.tickRate);
                    players_[pj.player.id] = std::move(rp);
                    log("guest: player " + std::to_string(pj.player.id) + " '" + pj.player.nick + "' joined");
                    if (onPlayerJoined) onPlayerJoined(pj.player);
                    break;
                }
                case MsgType::PlayerLeft: {
                    PlayerLeft pl;
                    if (!decodePacket(ev.data, pl)) return;
                    players_.erase(pl.id);
                    log("guest: player " + std::to_string(pl.id) + " left");
                    if (onPlayerLeft) onPlayerLeft(pl.id, pl.reason);
                    break;
                }
                case MsgType::WorldSnapshot:
                    guestOnSnapshot(ev.data, nowMs);
                    break;
                case MsgType::Pong: {
                    Pong p;
                    if (!decodePacket(ev.data, p)) return;
                    clock_.onPong(p.hostTick, nowMs, nowMs - p.clientTimeMs);
                    break;
                }
                case MsgType::Chat: {
                    Chat c;
                    if (!decodePacket(ev.data, c)) return;
                    if (onChat) onChat(c.fromId, c.text);
                    break;
                }
                case MsgType::HostSettings: {
                    HostSettings hs;
                    if (!decodePacket(ev.data, hs)) return;
                    cfg_.hostFlags = hs.hostFlags;
                    if (hs.tickRate) { cfg_.tickRate = hs.tickRate; clock_.setTickRate(hs.tickRate); }
                    break;
                }
                default: break;
            }
            break;
        }
    }
}

void Session::guestOnSnapshot(std::span<const uint8_t> pkt, uint32_t nowMs) {
    WorldSnapshot snap;
    const auto res = decodeSnapshot(pkt, snap, [&](uint32_t t) { return recvHistory_.find(t); });
    if (res == SnapshotDecodeResult::UnknownBaseline) {
        desyncs_++;
        ackedSnapshotTick_ = 0;  // ask for a full snapshot
        return;
    }
    if (res != SnapshotDecodeResult::Ok) return;
    if (snap.hostTick <= hostTick_ && hostTick_ - snap.hostTick < 1000) return;  // stale/out of order
    hostTick_ = snap.hostTick;
    ackedSnapshotTick_ = snap.hostTick;
    clock_.onSnapshot(snap.hostTick, nowMs);
    for (const auto& e : snap.entries) {
        if (e.id == localId_) continue;  // v0: no reconciliation for self
        auto it = players_.find(e.id);
        if (it == players_.end()) {
            RemotePlayer rp; rp.id = e.id; rp.nick = "player" + std::to_string(e.id);
            rp.interp.setTickRateHint(cfg_.tickRate);
            it = players_.emplace(e.id, std::move(rp)).first;
        }
        it->second.last = e.state;
        it->second.hasState = true;
        it->second.lastSeenMs = nowMs;
        it->second.interp.push(e.state);
    }
    recvHistory_.push(std::move(snap));
    statSnapshots_++;
}

void Session::guestSendState(uint32_t nowMs) {
    lastStateSentMs_ = nowMs;
    if (!hasLocalState_) return;
    PlayerStateMsg m; m.ackedSnapshotTick = ackedSnapshotTick_; m.state = localState_;
    transport_->send(hostPeer_, kChannelState, encode(m), false);
    statStates_++;
}

void Session::sendChat(const std::string& text) {
    Chat c; c.fromId = localId_; c.text = text;
    if (role_ == Role::Host) {
        if (onChat) onChat(0, text);
        hostBroadcast(encode(c), kChannelControl, true, kInvalidPeer);
    } else if (state_ == State::Connected) {
        transport_->send(hostPeer_, kChannelControl, encode(c), true);
    }
}

// ---------------------------------------------------------------------------
std::optional<PlayerState> Session::remoteStateAt(uint8_t id, uint32_t nowMs) const {
    auto it = players_.find(id);
    if (it == players_.end() || !it->second.hasState) return std::nullopt;
    return it->second.interp.sample(estimatedHostTick(nowMs));
}

double Session::estimatedHostTick(uint32_t nowMs) const {
    if (role_ == Role::Host) {
        const double frac = static_cast<double>(nowMs - lastTickMs_) * cfg_.tickRate / 1000.0;
        return static_cast<double>(hostTick_) + std::min(frac, 1.0);
    }
    return clock_.synced() ? clock_.hostTickAt(nowMs) : static_cast<double>(hostTick_);
}

SessionStats Session::stats() const {
    SessionStats s;
    s.hostTick = hostTick_;
    s.snapshotsPerSec = snapshotsPerSec_;
    s.statesPerSec = statesPerSec_;
    s.desyncs = desyncs_;
    s.transport = transport_->stats();
    s.relayRttMs = useRelay_ ? relay_.relayRttMs() : 0;
    if (role_ == Role::Host) {
        for (auto& [id, p] : players_) s.rttMs = std::max(s.rttMs, p.rttMs);
    } else {
        s.rttMs = clock_.rttMs() ? clock_.rttMs() : transport_->roundTripMs(hostPeer_);
    }
    return s;
}

std::string Session::statusText() const {
    std::string s = role_ == Role::Host ? "HOST" : role_ == Role::Guest ? "GUEST" : "-";
    s += " / "; s += stateName(state_);
    if (useRelay_ && !relay_.roomCode().empty()) s += " / room " + relay_.roomCode();
    if (!error_.empty() && state_ == State::Failed) s += " / " + error_;
    return s;
}

}  // namespace dsnet
