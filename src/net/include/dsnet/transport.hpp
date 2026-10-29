#pragma once
// Transport abstraction so Session can run over ENet in the game and over an in-memory
// loopback in unit tests. Also exposes raw datagram I/O on the same socket for the relay
// control protocol (DSCP).
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dsnet {

using PeerId = uint32_t;
constexpr PeerId kInvalidPeer = 0xFFFFFFFFu;

struct Endpoint {
    std::string host;  // hostname or dotted IPv4
    uint16_t port = 0;
    bool operator==(const Endpoint&) const = default;
    std::string toString() const { return host + ":" + std::to_string(port); }
    /// "host:port" -> Endpoint. Returns nullopt on malformed input.
    static std::optional<Endpoint> parse(std::string_view s, uint16_t defaultPort = 0);
};

enum class EventType { Connected, Disconnected, Data };

struct Event {
    EventType type = EventType::Data;
    PeerId peer = kInvalidPeer;
    uint8_t channel = 0;
    uint32_t data32 = 0;  // disconnect reason / connect data
    std::vector<uint8_t> data;
};

struct TransportStats {
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint64_t packetsSent = 0;
    uint64_t packetsReceived = 0;
    uint64_t packetsLost = 0;
};

class ITransport {
public:
    using RawHandler = std::function<bool(const Endpoint& from, std::span<const uint8_t> data)>;

    virtual ~ITransport() = default;

    /// Server: bind `port` (0 = any) and accept up to maxPeers connections.
    virtual bool listen(uint16_t port, size_t maxPeers, size_t channels, std::string* err) = 0;
    /// Client: open an unbound socket (ephemeral port) able to connect to one peer.
    virtual bool open(size_t channels, std::string* err) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual PeerId connect(const Endpoint& to, size_t channels, std::string* err) = 0;
    virtual void disconnect(PeerId peer, uint32_t data32) = 0;

    /// Pump the socket; fills `out` with events. `nowMs` is the caller's monotonic clock.
    virtual void service(uint32_t nowMs, std::vector<Event>& out) = 0;
    virtual bool send(PeerId peer, uint8_t channel, std::span<const uint8_t> data, bool reliable) = 0;
    virtual void flush() = 0;

    /// Raw datagram to an arbitrary endpoint on the same socket (relay control).
    virtual bool sendRaw(const Endpoint& to, std::span<const uint8_t> data) = 0;
    /// Handler sees every incoming datagram first; return true to consume it.
    virtual void setRawHandler(RawHandler handler) = 0;

    virtual uint32_t roundTripMs(PeerId peer) const = 0;
    virtual uint16_t localPort() const = 0;
    virtual TransportStats stats() const = 0;
    virtual const char* name() const = 0;
};

/// Monotonic milliseconds (wraps every ~49 days, which the protocol tolerates).
uint32_t nowMs();

}  // namespace dsnet
