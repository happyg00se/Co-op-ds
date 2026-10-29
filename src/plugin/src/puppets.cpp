#include "puppets.hpp"

#include <cmath>
#include <cstring>

namespace dscoop {
using namespace dsproto;

WorldTransformLayout WorldTransformLayout::fromJson(const nlohmann::json& j) {
    WorldTransformLayout l;
    if (!j.is_object()) return l;
    l.size = j.value("size", l.size);
    l.rotationOffset = j.value("rotation_offset", l.rotationOffset);
    l.positionOffset = j.value("position_offset", l.positionOffset);
    l.verified = j.value("verified", false);
    return l;
}

// ---------------------------------------------------------------------------
DecimaBackend::DecimaBackend(const DSLoaderApi& api, const nlohmann::json& offsets, Resolver resolve, PointerReader readPtr)
    : api_(api), readPtr_(std::move(readPtr)) {
    if (offsets.contains("layouts") && offsets["layouts"].is_object())
        layout_ = WorldTransformLayout::fromJson(offsets["layouts"].value("world_transform", nlohmann::json::object()));
    setTransform_ = reinterpret_cast<SetWorldTransformFn>(resolve("Entity_SetWorldTransform"));
    spawn_ = reinterpret_cast<SpawnFromResourceFn>(resolve("Entity_SpawnFromResource"));
    destroy_ = reinterpret_cast<DestroyFn>(resolve("Entity_Destroy"));
    setLocomotion_ = reinterpret_cast<SetLocomotionFn>(resolve("Mover_SetLocomotion"));
    if (!setTransform_) missing_ += "Entity_SetWorldTransform ";
    if (!spawn_) missing_ += "Entity_SpawnFromResource ";
    if (!layout_.verified) missing_ += "layouts.world_transform(verified) ";
}

bool DecimaBackend::available(std::string* why) const {
    if (!missing_.empty()) { if (why) *why = "missing: " + missing_; return false; }
    if (!readPtr_("local_player_entity_resource")) { if (why) *why = "local_player_entity_resource does not resolve (not in world yet?)"; return false; }
    return true;
}

void DecimaBackend::buildTransform(const PlayerState& s, std::vector<uint8_t>& buf) const {
    buf.assign(layout_.size, 0);
    // Rotation: yaw around Z (Decima is Z-up). Rows = right, forward, up (see GameBridge camera rows).
    const float c = std::cos(s.yaw), sn = std::sin(s.yaw);
    const float rot[9] = {c, sn, 0, -sn, c, 0, 0, 0, 1};
    std::memcpy(buf.data() + layout_.rotationOffset, rot, sizeof(rot));
    const double pos[3] = {s.pos.x, s.pos.y, s.pos.z};
    std::memcpy(buf.data() + layout_.positionOffset, pos, sizeof(pos));
}

bool DecimaBackend::spawn(uint8_t playerId, const PlayerState& at, PuppetHandle& out) {
    std::string why;
    if (!available(&why)) return false;
    auto resPtrAddr = readPtr_("local_player_entity_resource");
    void* resource = nullptr;
    if (!resPtrAddr || !api_.safe_read(reinterpret_cast<const void*>(*resPtrAddr), &resource, sizeof(resource)) || !resource) return false;
    std::vector<uint8_t> wt;
    buildTransform(at, wt);
    void* entity = spawn_(resource, wt.data());
    if (!entity) return false;
    out.entity = entity;
    char msg[128];
    std::snprintf(msg, sizeof(msg), "puppet for player %u spawned at %.1f %.1f %.1f", playerId, at.pos.x, at.pos.y, at.pos.z);
    api_.log(DSLOG_INFO, "DSCoop/puppets", msg);
    return true;
}

void DecimaBackend::destroy(PuppetHandle& h) {
    if (h.entity && destroy_) destroy_(h.entity);
    h.entity = nullptr;
}

void DecimaBackend::apply(PuppetHandle& h, const PlayerState& s, float) {
    if (!h.entity) return;
    std::vector<uint8_t> wt;
    buildTransform(s, wt);
    setTransform_(h.entity, wt.data());
    if (setLocomotion_) {
        const float speed = s.vel.length();
        const float dir = std::atan2(s.vel.y, s.vel.x) - s.yaw;  // movement direction relative to facing
        setLocomotion_(h.entity, speed, dir, s.flags);
    }
}

// ---------------------------------------------------------------------------
void PuppetManager::tick(dsnet::Session& session, uint32_t nowMs, float dtSec) {
    if (!backend_) return;
    // Remove puppets of players that left.
    for (auto it = puppets_.begin(); it != puppets_.end();) {
        if (!session.players().count(it->first)) { backend_->destroy(it->second); it = puppets_.erase(it); }
        else ++it;
    }
    if (!enabled_) return;
    for (const auto& [id, p] : session.players()) {
        auto st = session.remoteStateAt(id, nowMs);
        if (!st) continue;
        auto it = puppets_.find(id);
        if (it == puppets_.end()) {
            if (nowMs - lastSpawnAttemptMs_ < 1000) continue;  // don't hammer a failing spawn
            lastSpawnAttemptMs_ = nowMs;
            PuppetHandle h;
            if (backend_->spawn(id, *st, h)) { h.spawnedAtMs = nowMs; puppets_[id] = h; }
            else ++spawnFailures_;
            continue;
        }
        backend_->apply(it->second, *st, dtSec);
    }
}

void PuppetManager::clear() {
    if (backend_) for (auto& [id, h] : puppets_) backend_->destroy(h);
    puppets_.clear();
}

std::string PuppetManager::status() const {
    if (!backend_) return "no backend";
    std::string why;
    if (!backend_->available(&why)) return std::string(backend_->name()) + ": unavailable (" + why + ")";
    return std::string(backend_->name()) + ": " + std::to_string(puppets_.size()) + " puppets" +
           (spawnFailures_ ? ", " + std::to_string(spawnFailures_) + " spawn failures" : "");
}

}  // namespace dscoop
