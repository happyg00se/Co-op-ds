#pragma once
// Client side of the DSCP relay protocol (see dsproto/relay_control.hpp and relay/relay.py).
// Runs on top of ITransport::sendRaw / RawHandler so ENet traffic and control traffic share
// one UDP socket — that is what lets the relay learn each peer's NAT mapping.
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "dsnet/transport.hpp"
#include "dsproto/relay_control.hpp"

namespace dsnet {

class RelayClient {
public:
    enum class Phase { Idle, Creating, Joining, Active, Failed };

    struct Options {
        Endpoint relay;                 // relay host + control port
        uint32_t clientVersion = 0;
        uint8_t maxGuests = dsproto::relay::kMaxGuests;
        uint32_t requestRetryMs = 1000; // CREATE/JOIN retransmit
        uint32_t requestTimeoutMs = 8000;
        uint32_t pingIntervalMs = 2000; // NAT keep-alive + mapping learn
        uint32_t pongTimeoutMs = 15000; // no PONG for this long = relay lost
    };

    void beginCreate(ITransport& t, const Options& opts, uint32_t nowMs);
    void beginJoin(ITransport& t, const Options& opts, const std::string& roomCode, uint32_t nowMs);
    void stop(uint32_t nowMs);  // host: sends CLOSE
    void update(uint32_t nowMs);
    /// Feed every incoming raw datagram here (wired through ITransport::setRawHandler). Returns true if consumed.
    bool handleRaw(const Endpoint& from, std::span<const uint8_t> data, uint32_t nowMs);

    Phase phase() const { return phase_; }
    bool isHost() const { return isHost_; }
    const std::string& roomCode() const { return roomCode_; }
    const std::string& error() const { return error_; }
    /// Guest: endpoint on the relay to ENet-connect to; valid once readyToConnect().
    std::optional<Endpoint> hostEndpoint() const;
    /// Guest: the relay has acknowledged our PING on the host port, so ENet traffic will be forwarded.
    bool readyToConnect() const { return phase_ == Phase::Active && !isHost_ && pongsReceived_ > 0; }
    uint32_t lastPongAgeMs(uint32_t nowMs) const { return lastPongMs_ ? nowMs - lastPongMs_ : 0; }
    uint32_t relayRttMs() const { return rttMs_; }

    std::function<void(const std::string&)> onLog;

private:
    void fail(const std::string& why);
    void sendRequest(uint32_t nowMs);
    void sendPings(uint32_t nowMs);
    void log(const std::string& s) { if (onLog) onLog(s); }

    ITransport* transport_ = nullptr;
    Options opts_;
    Phase phase_ = Phase::Idle;
    bool isHost_ = false;
    std::string roomCode_;
    std::string error_;
    uint32_t startedMs_ = 0;
    uint32_t lastRequestMs_ = 0;
    uint32_t lastPingMs_ = 0;
    uint32_t lastPongMs_ = 0;
    uint32_t pongsReceived_ = 0;
    uint32_t rttMs_ = 0;
    uint32_t pingSentMs_ = 0;
    // host side
    uint64_t hostToken_ = 0;
    uint16_t hostPort_ = 0;
    std::vector<uint16_t> guestPorts_;
    // guest side
    uint8_t slot_ = 0;
    uint64_t slotToken_ = 0;
};

}  // namespace dsnet
