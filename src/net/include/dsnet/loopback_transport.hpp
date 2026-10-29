#pragma once
// In-memory transport for unit tests: deterministic, with optional latency and loss.
#include <deque>
#include <map>
#include <memory>
#include <random>

#include "dsnet/transport.hpp"

namespace dsnet {

class LoopbackNetwork;

class LoopbackTransport final : public ITransport {
public:
    explicit LoopbackTransport(std::shared_ptr<LoopbackNetwork> net);
    ~LoopbackTransport() override;

    bool listen(uint16_t port, size_t maxPeers, size_t channels, std::string* err) override;
    bool open(size_t channels, std::string* err) override;
    void close() override;
    bool isOpen() const override { return open_; }

    PeerId connect(const Endpoint& to, size_t channels, std::string* err) override;
    void disconnect(PeerId peer, uint32_t data32) override;

    void service(uint32_t nowMs, std::vector<Event>& out) override;
    bool send(PeerId peer, uint8_t channel, std::span<const uint8_t> data, bool reliable) override;
    void flush() override {}

    bool sendRaw(const Endpoint&, std::span<const uint8_t>) override { return false; }
    void setRawHandler(RawHandler) override {}

    uint32_t roundTripMs(PeerId) const override;
    uint16_t localPort() const override { return port_; }
    TransportStats stats() const override { return stats_; }
    const char* name() const override { return "loopback"; }

private:
    friend class LoopbackNetwork;
    struct Link { LoopbackTransport* remote = nullptr; PeerId remotePeer = kInvalidPeer; bool connected = false; };
    struct Pending { uint32_t deliverAt; Event ev; };

    PeerId allocPeer();
    void enqueue(Event ev, uint32_t nowMs);

    std::shared_ptr<LoopbackNetwork> net_;
    bool open_ = false;
    uint16_t port_ = 0;
    size_t maxPeers_ = 1;
    std::map<PeerId, Link> links_;
    std::deque<Pending> inbox_;
    TransportStats stats_;
    uint32_t lastServiceMs_ = 0;
};

class LoopbackNetwork : public std::enable_shared_from_this<LoopbackNetwork> {
public:
    uint32_t latencyMs = 0;   // one-way
    float lossRate = 0.0f;    // applied to unreliable packets only
    std::mt19937 rng{12345};

    uint32_t currentTimeMs = 0;  // transports stamp with their service() clock

private:
    friend class LoopbackTransport;
    std::map<uint16_t, LoopbackTransport*> listeners_;
    uint16_t nextEphemeral_ = 50000;
};

}  // namespace dsnet
