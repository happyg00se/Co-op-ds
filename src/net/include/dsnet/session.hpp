#pragma once
// Session = the DSCoop game session state machine (host or guest) on top of ITransport.
// Host is the single source of truth: it assigns player ids, builds WorldSnapshots at
// tickRate and validates guest input (v0: positions only). Guests send PlayerState and
// render others through PlayerInterpolator.
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "dsnet/relay_client.hpp"
#include "dsnet/transport.hpp"
#include "dsproto/interp.hpp"
#include "dsproto/messages.hpp"
#include "dsproto/snapshot.hpp"

namespace dsnet {

struct SessionConfig {
    std::string nick = "Porter";
    uint32_t clientVersion = 0;
    uint32_t gameBuild = 0;
    uint32_t features = 0;          // dsproto::ClientFeatures
    uint8_t maxPlayers = dsproto::kMaxPlayers;
    uint32_t tickRate = dsproto::kDefaultTickRate;
    uint32_t hostFlags = 0;         // dsproto::HostFlags
    uint32_t helloTimeoutMs = 5000; // host: peer must send Hello within this
    uint32_t peerTimeoutMs = 10000; // no packets for this long -> drop
    uint32_t pingIntervalMs = 1000;
    bool strictVersion = true;      // reject different clientVersion
};

struct RelayOptions {
    std::string host;
    uint16_t controlPort = dsproto::relay::kDefaultControlPort;
};

struct RemotePlayer {
    uint8_t id = dsproto::kInvalidPlayerId;
    std::string nick;
    dsproto::PlayerState last;      // newest raw state
    dsproto::PlayerInterpolator interp;
    uint32_t lastSeenMs = 0;
    uint32_t rttMs = 0;
    // host-only bookkeeping
    PeerId peer = kInvalidPeer;
    uint32_t ackedSnapshotTick = 0;
    bool hasState = false;
};

struct SessionStats {
    uint32_t hostTick = 0;
    uint32_t rttMs = 0;              // guest: to host; host: max over guests
    uint32_t snapshotsPerSec = 0;
    uint32_t statesPerSec = 0;
    uint32_t desyncs = 0;            // unknown-baseline events
    uint32_t relayRttMs = 0;
    TransportStats transport;
};

class Session {
public:
    enum class Role { None, Host, Guest };
    enum class State { Idle, RelayHandshake, Connecting, Handshaking, Connected, Failed, Closed };

    explicit Session(std::unique_ptr<ITransport> transport);
    ~Session();

    bool host(const SessionConfig& cfg, uint16_t listenPort, const std::optional<RelayOptions>& relay, std::string* err);
    bool joinDirect(const SessionConfig& cfg, const Endpoint& hostEp, std::string* err);
    bool joinRelay(const SessionConfig& cfg, const RelayOptions& relay, const std::string& roomCode, std::string* err);
    void leave();

    /// Call every frame (or at >= tickRate) with a monotonic clock.
    void update(uint32_t nowMs);

    void setLocalState(const dsproto::PlayerState& s) { localState_ = s; hasLocalState_ = true; }
    void sendChat(const std::string& text);

    Role role() const { return role_; }
    State state() const { return state_; }
    uint8_t localId() const { return localId_; }
    const std::string& roomCode() const { return relay_.roomCode(); }
    const std::string& error() const { return error_; }
    std::string statusText() const;
    const std::map<uint8_t, RemotePlayer>& players() const { return players_; }
    /// Interpolated state of a remote player for rendering, or nullopt.
    std::optional<dsproto::PlayerState> remoteStateAt(uint8_t id, uint32_t nowMs) const;
    double estimatedHostTick(uint32_t nowMs) const;
    SessionStats stats() const;
    ITransport& transport() { return *transport_; }
    uint32_t hostTick() const { return hostTick_; }

    // Callbacks (all invoked from update()).
    std::function<void(const std::string&)> onLog;
    std::function<void(const dsproto::PlayerInfo&)> onPlayerJoined;
    std::function<void(uint8_t id, uint8_t reason)> onPlayerLeft;
    std::function<void(uint8_t from, const std::string&)> onChat;
    std::function<void(State)> onStateChanged;

private:
    struct PendingPeer { uint32_t connectedAtMs; };

    void setState(State s);
    void fail(const std::string& why);
    void log(const std::string& s) { if (onLog) onLog(s); }

    // host
    void hostHandleEvent(const Event& ev, uint32_t nowMs);
    void hostHandleHello(PeerId peer, std::span<const uint8_t> pkt, uint32_t nowMs);
    void hostTickOnce(uint32_t nowMs);
    void hostDropPlayer(uint8_t id, uint8_t reason, uint32_t nowMs);
    void hostBroadcast(const std::vector<uint8_t>& pkt, uint8_t channel, bool reliable, PeerId except);
    uint8_t allocPlayerId() const;
    RemotePlayer* playerByPeer(PeerId peer);

    // guest
    void guestHandleEvent(const Event& ev, uint32_t nowMs);
    void guestSendState(uint32_t nowMs);
    void guestOnSnapshot(std::span<const uint8_t> pkt, uint32_t nowMs);

    std::unique_ptr<ITransport> transport_;
    SessionConfig cfg_;
    Role role_ = Role::None;
    State state_ = State::Idle;
    std::string error_;
    uint8_t localId_ = dsproto::kInvalidPlayerId;
    uint64_t sessionId_ = 0;

    dsproto::PlayerState localState_;
    bool hasLocalState_ = false;

    std::map<uint8_t, RemotePlayer> players_;
    std::map<PeerId, PendingPeer> pending_;

    // host
    uint32_t hostTick_ = 0;
    uint32_t lastTickMs_ = 0;
    dsproto::SnapshotHistory sentHistory_{64};

    // guest
    PeerId hostPeer_ = kInvalidPeer;
    uint32_t connectStartedMs_ = 0;
    uint32_t lastStateSentMs_ = 0;
    uint32_t lastPingMs_ = 0;
    uint32_t lastFromHostMs_ = 0;
    uint32_t ackedSnapshotTick_ = 0;
    dsproto::SnapshotHistory recvHistory_{64};
    dsproto::TickClock clock_;

    // relay
    bool useRelay_ = false;
    RelayOptions relayOpts_;
    RelayClient relay_;

    // stats
    uint32_t statWindowStartMs_ = 0;
    uint32_t statSnapshots_ = 0, statStates_ = 0, snapshotsPerSec_ = 0, statesPerSec_ = 0;
    uint32_t desyncs_ = 0;
};

}  // namespace dsnet
