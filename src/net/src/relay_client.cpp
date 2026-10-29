#include "dsnet/relay_client.hpp"

namespace dsnet {
using namespace dsproto::relay;

void RelayClient::beginCreate(ITransport& t, const Options& opts, uint32_t nowMs) {
    transport_ = &t; opts_ = opts; isHost_ = true; phase_ = Phase::Creating;
    roomCode_.clear(); error_.clear(); guestPorts_.clear();
    startedMs_ = nowMs; lastRequestMs_ = 0; pongsReceived_ = 0; lastPongMs_ = 0;
    sendRequest(nowMs);
}

void RelayClient::beginJoin(ITransport& t, const Options& opts, const std::string& roomCode, uint32_t nowMs) {
    transport_ = &t; opts_ = opts; isHost_ = false; phase_ = Phase::Joining;
    roomCode_ = normalizeRoomCode(roomCode); error_.clear();
    startedMs_ = nowMs; lastRequestMs_ = 0; pongsReceived_ = 0; lastPongMs_ = 0;
    if (!isValidRoomCode(roomCode_)) { fail("invalid room code '" + roomCode_ + "' (6 chars, no 0/O/1/I/L)"); return; }
    sendRequest(nowMs);
}

void RelayClient::stop(uint32_t) {
    if (transport_ && isHost_ && hostToken_ && phase_ == Phase::Active)
        transport_->sendRaw(opts_.relay, encodeClose(hostToken_));
    phase_ = Phase::Idle;
    transport_ = nullptr;
}

void RelayClient::fail(const std::string& why) {
    error_ = why;
    phase_ = Phase::Failed;
    log("relay: " + why);
}

void RelayClient::sendRequest(uint32_t nowMs) {
    if (!transport_) return;
    lastRequestMs_ = nowMs;
    if (isHost_) transport_->sendRaw(opts_.relay, encodeCreate(opts_.maxGuests, opts_.clientVersion));
    else transport_->sendRaw(opts_.relay, encodeJoin(roomCode_, opts_.clientVersion));
}

void RelayClient::sendPings(uint32_t nowMs) {
    if (!transport_) return;
    lastPingMs_ = nowMs;
    pingSentMs_ = nowMs;
    if (isHost_) {
        for (size_t i = 0; i < guestPorts_.size(); ++i)
            transport_->sendRaw(Endpoint{opts_.relay.host, guestPorts_[i]}, encodePing(hostToken_, static_cast<uint8_t>(i + 1)));
    } else {
        transport_->sendRaw(Endpoint{opts_.relay.host, hostPort_}, encodePing(slotToken_, slot_));
    }
}

void RelayClient::update(uint32_t nowMs) {
    switch (phase_) {
        case Phase::Creating:
        case Phase::Joining:
            if (nowMs - startedMs_ > opts_.requestTimeoutMs) { fail("no answer from relay " + opts_.relay.toString()); return; }
            if (nowMs - lastRequestMs_ >= opts_.requestRetryMs) sendRequest(nowMs);
            break;
        case Phase::Active: {
            // First pings go out immediately (fast NAT mapping), then every pingIntervalMs.
            const uint32_t interval = pongsReceived_ == 0 ? 250 : opts_.pingIntervalMs;
            if (nowMs - lastPingMs_ >= interval) sendPings(nowMs);
            if (lastPongMs_ && nowMs - lastPongMs_ > opts_.pongTimeoutMs) fail("relay stopped answering pings");
            else if (!lastPongMs_ && nowMs - startedMs_ > opts_.requestTimeoutMs * 2) fail("relay never answered pings");
            break;
        }
        default: break;
    }
}

bool RelayClient::handleRaw(const Endpoint& from, std::span<const uint8_t> data, uint32_t nowMs) {
    const auto op = peekOp(data);
    if (!op) return false;  // not DSCP -> let ENet have it
    (void)from;
    switch (*op) {
        case Op::Created: {
            Created c;
            if (!decodeCreated(data, c)) { log("relay: malformed CREATED"); return true; }
            if (phase_ != Phase::Creating) return true;  // duplicate answer to a retransmit
            roomCode_ = c.code; hostToken_ = c.hostToken; hostPort_ = c.hostPort; guestPorts_ = c.guestPorts;
            phase_ = Phase::Active;
            lastPingMs_ = 0;
            log("relay: room " + roomCode_ + " created, host port " + std::to_string(hostPort_) + ", " +
                std::to_string(guestPorts_.size()) + " guest slots");
            return true;
        }
        case Op::Joined: {
            Joined j;
            if (!decodeJoined(data, j)) { log("relay: malformed JOINED"); return true; }
            if (phase_ != Phase::Joining) return true;
            hostPort_ = j.hostPort; slot_ = j.slot; slotToken_ = j.slotToken;
            phase_ = Phase::Active;
            lastPingMs_ = 0;
            log("relay: joined room " + roomCode_ + " as slot " + std::to_string(slot_) + ", host port " + std::to_string(hostPort_));
            return true;
        }
        case Op::Error: {
            Error e;
            if (decodeError(data, e)) fail(std::string(errorText(e.code)) + (e.text.empty() ? "" : ": " + e.text));
            else fail("relay error (malformed)");
            return true;
        }
        case Op::Pong: {
            Pong p;
            if (!decodePong(data, p)) return true;
            pongsReceived_++;
            lastPongMs_ = nowMs;
            const uint32_t rtt = nowMs - pingSentMs_;
            rttMs_ = rttMs_ ? (rttMs_ * 3 + rtt) / 4 : rtt;
            if (pongsReceived_ == 1) log("relay: mapping confirmed (pong, rtt " + std::to_string(rtt) + " ms)");
            return true;
        }
        default:
            return true;  // server-only ops arriving at a client: swallow
    }
}

std::optional<Endpoint> RelayClient::hostEndpoint() const {
    if (phase_ != Phase::Active || isHost_ || hostPort_ == 0) return std::nullopt;
    return Endpoint{opts_.relay.host, hostPort_};
}

}  // namespace dsnet
