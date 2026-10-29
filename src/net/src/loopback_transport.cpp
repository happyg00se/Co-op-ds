#include "dsnet/loopback_transport.hpp"

namespace dsnet {

LoopbackTransport::LoopbackTransport(std::shared_ptr<LoopbackNetwork> net) : net_(std::move(net)) {}
LoopbackTransport::~LoopbackTransport() { close(); }

bool LoopbackTransport::listen(uint16_t port, size_t maxPeers, size_t, std::string* err) {
    close();
    if (port == 0) port = net_->nextEphemeral_++;
    if (net_->listeners_.count(port)) {
        if (err) *err = "loopback port busy";
        return false;
    }
    net_->listeners_[port] = this;
    port_ = port;
    maxPeers_ = maxPeers;
    open_ = true;
    return true;
}

bool LoopbackTransport::open(size_t, std::string*) {
    close();
    port_ = net_->nextEphemeral_++;
    maxPeers_ = 1;
    open_ = true;
    return true;
}

void LoopbackTransport::close() {
    if (!open_) return;
    for (auto& [id, link] : links_) {
        if (link.remote && link.connected) {
            Event e; e.type = EventType::Disconnected; e.peer = link.remotePeer;
            link.remote->links_[link.remotePeer].connected = false;
            link.remote->links_[link.remotePeer].remote = nullptr;
            link.remote->enqueue(std::move(e), net_->currentTimeMs);
        }
    }
    links_.clear();
    net_->listeners_.erase(port_);
    open_ = false;
}

PeerId LoopbackTransport::allocPeer() {
    for (PeerId i = 0; i < maxPeers_; ++i)
        if (!links_.count(i)) return i;
    return kInvalidPeer;
}

PeerId LoopbackTransport::connect(const Endpoint& to, size_t, std::string* err) {
    auto it = net_->listeners_.find(to.port);
    if (it == net_->listeners_.end() || it->second == this) {
        if (err) *err = "loopback: nobody listens on " + to.toString();
        return kInvalidPeer;
    }
    LoopbackTransport* remote = it->second;
    const PeerId mine = allocPeer();
    const PeerId theirs = remote->allocPeer();
    if (mine == kInvalidPeer || theirs == kInvalidPeer) {
        if (err) *err = "loopback: no free peer slots";
        return kInvalidPeer;
    }
    links_[mine] = Link{remote, theirs, true};
    remote->links_[theirs] = Link{this, mine, true};
    Event a; a.type = EventType::Connected; a.peer = mine;
    Event b; b.type = EventType::Connected; b.peer = theirs;
    enqueue(std::move(a), net_->currentTimeMs);
    remote->enqueue(std::move(b), net_->currentTimeMs);
    return mine;
}

void LoopbackTransport::disconnect(PeerId peer, uint32_t data32) {
    auto it = links_.find(peer);
    if (it == links_.end()) return;
    Link link = it->second;
    links_.erase(it);
    Event mine; mine.type = EventType::Disconnected; mine.peer = peer; mine.data32 = data32;
    enqueue(std::move(mine), net_->currentTimeMs);
    if (link.remote && link.connected) {
        link.remote->links_.erase(link.remotePeer);
        Event e; e.type = EventType::Disconnected; e.peer = link.remotePeer; e.data32 = data32;
        link.remote->enqueue(std::move(e), net_->currentTimeMs);
    }
}

void LoopbackTransport::enqueue(Event ev, uint32_t nowMs) {
    inbox_.push_back(Pending{nowMs + net_->latencyMs, std::move(ev)});
}

void LoopbackTransport::service(uint32_t nowMs, std::vector<Event>& out) {
    net_->currentTimeMs = nowMs;
    lastServiceMs_ = nowMs;
    while (!inbox_.empty() && inbox_.front().deliverAt <= nowMs) {
        out.push_back(std::move(inbox_.front().ev));
        inbox_.pop_front();
    }
}

bool LoopbackTransport::send(PeerId peer, uint8_t channel, std::span<const uint8_t> data, bool reliable) {
    auto it = links_.find(peer);
    if (it == links_.end() || !it->second.connected || !it->second.remote) return false;
    stats_.packetsSent++;
    stats_.bytesSent += data.size();
    if (!reliable && net_->lossRate > 0.0f) {
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        if (d(net_->rng) < net_->lossRate) { stats_.packetsLost++; return true; }
    }
    Event e; e.type = EventType::Data; e.peer = it->second.remotePeer; e.channel = channel;
    e.data.assign(data.begin(), data.end());
    it->second.remote->stats_.packetsReceived++;
    it->second.remote->stats_.bytesReceived += data.size();
    it->second.remote->enqueue(std::move(e), net_->currentTimeMs);
    return true;
}

uint32_t LoopbackTransport::roundTripMs(PeerId) const { return net_->latencyMs * 2; }

}  // namespace dsnet
