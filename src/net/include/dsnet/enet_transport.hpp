#pragma once
#include <memory>

#include "dsnet/transport.hpp"

struct _ENetHost;
struct _ENetPeer;
struct _ENetEvent;

namespace dsnet {

class EnetTransport final : public ITransport {
public:
    EnetTransport();
    ~EnetTransport() override;

    bool listen(uint16_t port, size_t maxPeers, size_t channels, std::string* err) override;
    bool open(size_t channels, std::string* err) override;
    void close() override;
    bool isOpen() const override { return host_ != nullptr; }

    PeerId connect(const Endpoint& to, size_t channels, std::string* err) override;
    void disconnect(PeerId peer, uint32_t data32) override;

    void service(uint32_t nowMs, std::vector<Event>& out) override;
    bool send(PeerId peer, uint8_t channel, std::span<const uint8_t> data, bool reliable) override;
    void flush() override;

    bool sendRaw(const Endpoint& to, std::span<const uint8_t> data) override;
    void setRawHandler(RawHandler handler) override { rawHandler_ = std::move(handler); }

    uint32_t roundTripMs(PeerId peer) const override;
    uint16_t localPort() const override;
    TransportStats stats() const override;
    const char* name() const override { return "enet"; }

private:
    static int interceptThunk(_ENetHost* host, _ENetEvent* event);
    void registerHost();
    _ENetPeer* peerOf(PeerId id) const;
    PeerId idOf(_ENetPeer* p) const;

    _ENetHost* host_ = nullptr;
    RawHandler rawHandler_;
    TransportStats stats_;
};

}  // namespace dsnet
