#pragma once
// Puppets — stage 2: a real second Sam in the host's world for every remote player.
//
// The remote player's interpolated PlayerState is pushed every game tick into an engine
// entity ("puppet") through a backend. Backends are data-driven (signatures/layouts from
// offsets.json) so the engine-specific part is filled by recon, not by rewriting code:
//
//   DecimaBackend  — spawns an entity from the local Sam's EntityResource via the engine's own
//                    spawn function, moves it with Entity_SetWorldTransform and feeds locomotion
//                    parameters (speed/direction) to its animation graph. Because it is the
//                    same resource the player uses, it carries the same collision capsule:
//                    pushing each other works out of the box.
//   NullBackend    — no engine functions resolved yet: counts what *would* be done, so the UI
//                    can show "puppets: waiting for Game_Tick / Entity_Spawn signatures".
//
// Engine calls happen ONLY inside the hooked game tick (GameTickHook), never on the render thread.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "dsloader/plugin_api.h"
#include "dsnet/session.hpp"
#include "dsproto/types.hpp"

namespace dscoop {

struct PuppetHandle {
    void* entity = nullptr;  // engine Entity*
    uint32_t spawnedAtMs = 0;
};

class IPuppetBackend {
public:
    virtual ~IPuppetBackend() = default;
    virtual const char* name() const = 0;
    /// False with a human-readable reason while signatures/pointers are missing.
    virtual bool available(std::string* why) const = 0;
    virtual bool spawn(uint8_t playerId, const dsproto::PlayerState& at, PuppetHandle& out) = 0;
    virtual void destroy(PuppetHandle& h) = 0;
    /// Called every game tick with the interpolated state for this puppet.
    virtual void apply(PuppetHandle& h, const dsproto::PlayerState& s, float dtSec) = 0;
};

/// Layout of Decima's WorldTransform as found by recon (offsets.json "layouts.world_transform").
struct WorldTransformLayout {
    uint32_t size = 0x50;          // bytes
    uint32_t rotationOffset = 0;   // 3x3 float, row-major
    uint32_t positionOffset = 0x30;  // 3 doubles
    bool verified = false;
    static WorldTransformLayout fromJson(const nlohmann::json& j);
};

class NullBackend final : public IPuppetBackend {
public:
    explicit NullBackend(std::string why) : why_(std::move(why)) {}
    const char* name() const override { return "null"; }
    bool available(std::string* why) const override { if (why) *why = why_; return false; }
    bool spawn(uint8_t, const dsproto::PlayerState&, PuppetHandle&) override { return false; }
    void destroy(PuppetHandle&) override {}
    void apply(PuppetHandle&, const dsproto::PlayerState&, float) override {}
private:
    std::string why_;
};

/// Data-driven Decima backend. Needs (from offsets.json):
///   signatures: Entity_SetWorldTransform, Entity_SpawnFromResource, Entity_Destroy (optional),
///               Mover_SetLocomotion (optional, for animation)
///   pointers:   local_player (Entity*), local_player_entity_resource (EntityResource* of Sam)
///   layouts:    world_transform
/// Function shapes below are the working assumption for recon step 8; adjust after disassembly.
class DecimaBackend final : public IPuppetBackend {
public:
    using Resolver = std::function<void*(const char* sigName)>;
    using PointerReader = std::function<std::optional<uintptr_t>(const std::string& pointerName)>;
    DecimaBackend(const DSLoaderApi& api, const nlohmann::json& offsets, Resolver resolve, PointerReader readPtr);

    const char* name() const override { return "decima"; }
    bool available(std::string* why) const override;
    bool spawn(uint8_t playerId, const dsproto::PlayerState& at, PuppetHandle& out) override;
    void destroy(PuppetHandle& h) override;
    void apply(PuppetHandle& h, const dsproto::PlayerState& s, float dtSec) override;

private:
    void buildTransform(const dsproto::PlayerState& s, std::vector<uint8_t>& buf) const;

    // Assumed shapes (recon step 8 confirms/corrects):
    using SetWorldTransformFn = void (*)(void* entity, const void* worldTransform);
    using SpawnFromResourceFn = void* (*)(void* entityResource, const void* worldTransform);
    using DestroyFn = void (*)(void* entity);
    using SetLocomotionFn = void (*)(void* entity, float speed, float direction, uint32_t flags);

    const DSLoaderApi& api_;
    WorldTransformLayout layout_;
    SetWorldTransformFn setTransform_ = nullptr;
    SpawnFromResourceFn spawn_ = nullptr;
    DestroyFn destroy_ = nullptr;
    SetLocomotionFn setLocomotion_ = nullptr;
    PointerReader readPtr_;
    std::string missing_;
};

class PuppetManager {
public:
    void init(std::unique_ptr<IPuppetBackend> backend) { backend_ = std::move(backend); }
    void setEnabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    /// Run from the game tick: spawn/destroy to match the session, apply interpolated states.
    void tick(dsnet::Session& session, uint32_t nowMs, float dtSec);
    /// Destroy every puppet (session left / shutdown). Must run on the game thread too.
    void clear();

    std::string status() const;
    size_t count() const { return puppets_.size(); }
    const IPuppetBackend* backend() const { return backend_.get(); }

private:
    std::unique_ptr<IPuppetBackend> backend_;
    std::map<uint8_t, PuppetHandle> puppets_;
    bool enabled_ = true;
    uint32_t spawnFailures_ = 0;
    uint32_t lastSpawnAttemptMs_ = 0;
};

}  // namespace dscoop
