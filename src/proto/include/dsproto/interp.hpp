#pragma once
// Guest-side smoothing: a jitter buffer of host snapshots per remote player, sampled
// `interpDelayTicks` behind the estimated host tick. Extrapolates with velocity for
// up to `maxExtrapolateTicks` when packets are late.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>

#include "dsproto/quant.hpp"
#include "dsproto/types.hpp"

namespace dsproto {

/// Estimates the host tick as a continuous value from Pong replies.
class TickClock {
public:
    void setTickRate(uint32_t hz) { tickRate_ = hz ? hz : kDefaultTickRateFallback; }
    uint32_t tickRate() const { return tickRate_; }

    /// Called when a Pong arrives. rttMs = now - clientTimeMs.
    void onPong(uint32_t hostTick, uint32_t nowMs, uint32_t rttMs) {
        rttMs_ = rttMs_ == 0 ? rttMs : (rttMs_ * 7 + rttMs) / 8;
        // Host tick at the moment this pong was produced ~ hostTick + rtt/2 worth of ticks.
        const double hostNow = static_cast<double>(hostTick) + (rttMs / 2.0) * tickRate_ / 1000.0;
        const double est = hostNow - static_cast<double>(nowMs) * tickRate_ / 1000.0;  // offset = hostTick - localTicks
        if (!hasSync_) { offset_ = est; hasSync_ = true; }
        else offset_ += (est - offset_) * 0.2;  // smooth
        lastSyncMs_ = nowMs;
    }
    /// Also usable when snapshots arrive (no RTT info): keep the estimate from drifting.
    void onSnapshot(uint32_t hostTick, uint32_t nowMs) {
        const double est = static_cast<double>(hostTick) - static_cast<double>(nowMs) * tickRate_ / 1000.0;
        if (!hasSync_) { offset_ = est; hasSync_ = true; lastSyncMs_ = nowMs; return; }
        // Snapshot arrival says "host tick is at least this": nudge upward only, slowly.
        if (est > offset_) offset_ += (est - offset_) * 0.1;
    }
    bool synced() const { return hasSync_; }
    uint32_t rttMs() const { return rttMs_; }
    double hostTickAt(uint32_t nowMs) const { return offset_ + static_cast<double>(nowMs) * tickRate_ / 1000.0; }

private:
    static constexpr uint32_t kDefaultTickRateFallback = 30;
    uint32_t tickRate_ = 30;
    double offset_ = 0;
    bool hasSync_ = false;
    uint32_t rttMs_ = 0;
    uint32_t lastSyncMs_ = 0;
};

class PlayerInterpolator {
public:
    struct Config {
        double interpDelayTicks = 3.0;     // 100 ms @ 30 Hz
        double maxExtrapolateTicks = 8.0;  // ~260 ms
        size_t maxBuffer = 32;
    };

    PlayerInterpolator() = default;
    explicit PlayerInterpolator(const Config& cfg) : cfg_(cfg) {}

    void push(const PlayerState& s) {
        // Drop out-of-order duplicates.
        if (!buf_.empty() && s.tick <= buf_.back().tick) {
            if (s.tick == buf_.back().tick) buf_.back() = s;
            return;
        }
        buf_.push_back(s);
        while (buf_.size() > cfg_.maxBuffer) buf_.pop_front();
    }
    void clear() { buf_.clear(); }
    bool empty() const { return buf_.empty(); }
    const PlayerState* latest() const { return buf_.empty() ? nullptr : &buf_.back(); }

    /// Sample the state at `hostTickNow` (continuous). Returns nullopt only if the buffer is empty.
    std::optional<PlayerState> sample(double hostTickNow) const {
        if (buf_.empty()) return std::nullopt;
        const double t = hostTickNow - cfg_.interpDelayTicks;
        // Before first sample: hold first.
        if (t <= buf_.front().tick) return buf_.front();
        // Find bracket.
        for (size_t i = 0; i + 1 < buf_.size(); ++i) {
            const auto& a = buf_[i];
            const auto& b = buf_[i + 1];
            if (t >= a.tick && t <= b.tick) {
                const double span = static_cast<double>(b.tick - a.tick);
                const float f = span > 0 ? static_cast<float>((t - a.tick) / span) : 1.0f;
                return blend(a, b, f);
            }
        }
        // After last sample: extrapolate with velocity, clamped.
        const auto& last = buf_.back();
        const double over = std::min(t - last.tick, cfg_.maxExtrapolateTicks);
        PlayerState s = last;
        const float dtSec = static_cast<float>(over / tickRateHint_);
        s.pos = last.pos + last.vel * dtSec;
        return s;
    }
    void setTickRateHint(uint32_t hz) { tickRateHint_ = hz ? static_cast<double>(hz) : 30.0; }

private:
    static PlayerState blend(const PlayerState& a, const PlayerState& b, float f) {
        PlayerState s = b;  // discrete fields from the newer sample
        s.pos = lerp(a.pos, b.pos, f);
        s.vel = lerp(a.vel, b.vel, f);
        s.yaw = quant::lerpAngle(a.yaw, b.yaw, f);
        s.pitch = a.pitch + (b.pitch - a.pitch) * f;
        s.health = a.health + (b.health - a.health) * f;
        s.stamina = a.stamina + (b.stamina - a.stamina) * f;
        return s;
    }
    Config cfg_{};
    std::deque<PlayerState> buf_;
    double tickRateHint_ = 30.0;
};

}  // namespace dsproto
