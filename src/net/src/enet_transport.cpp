#include "dsnet/enet_transport.hpp"

#include <enet/enet.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

namespace dsnet {

uint32_t nowMs() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

std::optional<Endpoint> Endpoint::parse(std::string_view s, uint16_t defaultPort) {
    Endpoint ep;
    const auto colon = s.rfind(':');
    if (colon == std::string_view::npos) {
        if (s.empty() || defaultPort == 0) return std::nullopt;
        ep.host = std::string(s);
        ep.port = defaultPort;
        return ep;
    }
    ep.host = std::string(s.substr(0, colon));
    const auto portStr = s.substr(colon + 1);
    if (ep.host.empty() || portStr.empty()) return std::nullopt;
    unsigned long p = 0;
    for (char c : portStr) {
        if (c < '0' || c > '9') return std::nullopt;
        p = p * 10 + static_cast<unsigned long>(c - '0');
        if (p > 65535) return std::nullopt;
    }
    if (p == 0) return std::nullopt;
    ep.port = static_cast<uint16_t>(p);
    return ep;
}

namespace {
std::mutex g_initMutex;
int g_initCount = 0;
// ENetHost has no user-data slot, so map hosts to transports for the intercept callback.
std::mutex g_hostsMutex;
std::map<_ENetHost*, EnetTransport*> g_hosts;

void enetAddRef() {
    std::lock_guard<std::mutex> lk(g_initMutex);
    if (g_initCount++ == 0) enet_initialize();
}
void enetRelease() {
    std::lock_guard<std::mutex> lk(g_initMutex);
    if (--g_initCount == 0) enet_deinitialize();
}

bool toEnetAddress(const Endpoint& ep, ENetAddress& out) {
    out.port = ep.port;
    if (enet_address_set_host_ip(&out, ep.host.c_str()) == 0) return true;
    return enet_address_set_host(&out, ep.host.c_str()) == 0;
}
Endpoint fromEnetAddress(const ENetAddress& a) {
    char buf[64] = {0};
    enet_address_get_host_ip(&a, buf, sizeof(buf));
    return Endpoint{buf, a.port};
}
}  // namespace

EnetTransport::EnetTransport() { enetAddRef(); }

EnetTransport::~EnetTransport() {
    close();
    enetRelease();
}

bool EnetTransport::listen(uint16_t port, size_t maxPeers, size_t channels, std::string* err) {
    close();
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    host_ = enet_host_create(&addr, maxPeers, channels, 0, 0);
    if (!host_) {
        if (err) *err = "enet_host_create failed (port " + std::to_string(port) + " busy or no permission)";
        return false;
    }
    registerHost();
    return true;
}

bool EnetTransport::open(size_t channels, std::string* err) {
    close();
    host_ = enet_host_create(nullptr, 1, channels, 0, 0);
    if (!host_) {
        if (err) *err = "enet_host_create (client) failed";
        return false;
    }
    registerHost();
    return true;
}

void EnetTransport::registerHost() {
    std::lock_guard<std::mutex> lk(g_hostsMutex);
    g_hosts[host_] = this;
    host_->intercept = &EnetTransport::interceptThunk;
}

void EnetTransport::close() {
    if (!host_) return;
    {
        std::lock_guard<std::mutex> lk(g_hostsMutex);
        g_hosts.erase(host_);
    }
    for (size_t i = 0; i < host_->peerCount; ++i) {
        ENetPeer* p = &host_->peers[i];
        if (p->state != ENET_PEER_STATE_DISCONNECTED) enet_peer_disconnect_now(p, 0);
    }
    enet_host_flush(host_);
    enet_host_destroy(host_);
    host_ = nullptr;
}

PeerId EnetTransport::connect(const Endpoint& to, size_t channels, std::string* err) {
    if (!host_) {
        if (err) *err = "transport not open";
        return kInvalidPeer;
    }
    ENetAddress addr;
    if (!toEnetAddress(to, addr)) {
        if (err) *err = "cannot resolve " + to.toString();
        return kInvalidPeer;
    }
    ENetPeer* peer = enet_host_connect(host_, &addr, channels, 0);
    if (!peer) {
        if (err) *err = "enet_host_connect failed (no free peer slots)";
        return kInvalidPeer;
    }
    return idOf(peer);
}

void EnetTransport::disconnect(PeerId peer, uint32_t data32) {
    if (ENetPeer* p = peerOf(peer)) enet_peer_disconnect(p, data32);
}

int EnetTransport::interceptThunk(_ENetHost* host, _ENetEvent*) {
    EnetTransport* self = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_hostsMutex);
        auto it = g_hosts.find(host);
        if (it != g_hosts.end()) self = it->second;
    }
    if (!self || !self->rawHandler_) return 0;
    const std::span<const uint8_t> data(host->receivedData, host->receivedDataLength);
    if (self->rawHandler_(fromEnetAddress(host->receivedAddress), data)) return 1;  // consumed
    return 0;
}

void EnetTransport::service(uint32_t, std::vector<Event>& out) {
    if (!host_) return;
    ENetEvent ev;
    // Drain everything that is ready; never block the game thread.
    while (enet_host_service(host_, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                Event e; e.type = EventType::Connected; e.peer = idOf(ev.peer); e.data32 = ev.data;
                out.push_back(std::move(e));
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                Event e; e.type = EventType::Disconnected; e.peer = idOf(ev.peer); e.data32 = ev.data;
                out.push_back(std::move(e));
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                Event e; e.type = EventType::Data; e.peer = idOf(ev.peer); e.channel = ev.channelID;
                e.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
                stats_.packetsReceived++;
                stats_.bytesReceived += ev.packet->dataLength;
                enet_packet_destroy(ev.packet);
                out.push_back(std::move(e));
                break;
            }
            default: break;
        }
    }
}

bool EnetTransport::send(PeerId peer, uint8_t channel, std::span<const uint8_t> data, bool reliable) {
    ENetPeer* p = peerOf(peer);
    if (!p || p->state != ENET_PEER_STATE_CONNECTED) return false;
    const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(), flags);
    if (!pkt) return false;
    if (enet_peer_send(p, channel, pkt) != 0) {
        enet_packet_destroy(pkt);
        return false;
    }
    stats_.packetsSent++;
    stats_.bytesSent += data.size();
    return true;
}

void EnetTransport::flush() {
    if (host_) enet_host_flush(host_);
}

bool EnetTransport::sendRaw(const Endpoint& to, std::span<const uint8_t> data) {
    if (!host_) return false;
    ENetAddress addr;
    if (!toEnetAddress(to, addr)) return false;
    ENetBuffer buf;
    buf.data = const_cast<uint8_t*>(data.data());
    buf.dataLength = data.size();
    return enet_socket_send(host_->socket, &addr, &buf, 1) >= 0;
}

uint32_t EnetTransport::roundTripMs(PeerId peer) const {
    const ENetPeer* p = peerOf(peer);
    return p ? p->roundTripTime : 0;
}

uint16_t EnetTransport::localPort() const {
    if (!host_) return 0;
    ENetAddress a;
    if (enet_socket_get_address(host_->socket, &a) == 0) return a.port;
    return host_->address.port;
}

TransportStats EnetTransport::stats() const {
    TransportStats s = stats_;
    if (host_) {
        for (size_t i = 0; i < host_->peerCount; ++i) s.packetsLost += host_->peers[i].packetsLost;
    }
    return s;
}

_ENetPeer* EnetTransport::peerOf(PeerId id) const {
    if (!host_ || id == kInvalidPeer || id >= host_->peerCount) return nullptr;
    return &host_->peers[id];
}
PeerId EnetTransport::idOf(_ENetPeer* p) const {
    if (!host_ || !p) return kInvalidPeer;
    return static_cast<PeerId>(p - host_->peers);
}

}  // namespace dsnet
