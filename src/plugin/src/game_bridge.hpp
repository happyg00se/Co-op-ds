#pragma once
// Reads the local Sam's state out of game memory using pointer chains from offsets.json
// ("pointers" section) and writes nothing (v0). Until recon fills the chains, isAttached()
// is false and the plugin runs in "fake" mode (radar + markers still work for remote players).
//
// offsets.json "pointers" entry:
//   "local_player_position": {
//       "base": "sig:PlayerManager",   // or "module", or "ptr:<other pointer entry>"
//       "add": 0,                      // added to base before following the chain
//       "chain": [0x38, 0x1C0],        // CE-style: addr = *(addr) + off  for each entry; value lives at the final addr
//       "type": "vec3f",               // vec3f | vec3d | f32 | quat_f32 | mat3_f32 | mat4_f32 | u32 | u64
//       "note": "WorldPosition of the player Entity (Decima uses doubles for world coords)"
//   }
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dsproto/types.hpp"

namespace dscoop {

struct Mat4 { float m[16]; };  // row-major as read; `layout` in json tells how to use it

class GameBridge {
public:
    using Resolver = std::function<uintptr_t(const std::string& sigName)>;
    using Reader = std::function<bool(const void* src, void* dst, size_t n)>;

    void init(const nlohmann::json& offsets, uintptr_t moduleBase, Resolver resolver, Reader reader);

    /// True when the position pointer chain resolves to readable memory.
    bool isAttached() const { return attached_; }
    const std::string& status() const { return status_; }

    /// Poll game memory. Call once per frame; cheap.
    void update();

    const dsproto::PlayerState& localState() const { return local_; }
    /// View-projection for world->screen. Either read directly (pointer "camera_view_proj") or
    /// built from "camera_rotation" (mat3_f32) + "camera_position" + "camera_fov" (vertical, degrees).
    std::optional<Mat4> viewProj() const { return viewProj_; }
    bool hasCamera() const { return viewProj_.has_value(); }
    std::optional<dsproto::Vec3> cameraPos() const { return camPos_; }
    void setAspect(float a) { aspect_ = a > 0 ? a : aspect_; }

    /// Resolved final addresses for the debug UI.
    struct PointerDebug { std::string name; uintptr_t address; std::string type; bool ok; std::string note; };
    std::vector<PointerDebug> debugPointers() const;

private:
    struct Chain { std::string base; int64_t add = 0; std::vector<int64_t> chain; std::string type; std::string note; };
    std::optional<uintptr_t> follow(const Chain& c) const;
    std::optional<uintptr_t> baseAddress(const std::string& base) const;

    std::map<std::string, Chain> chains_;
    uintptr_t moduleBase_ = 0;
    Resolver resolver_;
    Reader reader_;
    bool attached_ = false;
    std::string status_ = "offsets not loaded";
    dsproto::PlayerState local_;
    std::optional<Mat4> viewProj_;
    std::optional<dsproto::Vec3> camPos_;
    std::string matLayout_ = "row_major";
    float aspect_ = 16.0f / 9.0f;
    // Which rows of the 3x3 camera matrix are right/forward/up (Decima is Z-up; calibrated by recon step 5).
    int rightRow_ = 0, forwardRow_ = 1, upRow_ = 2;
    uint32_t frame_ = 0;
    bool readVec3(const Chain& c, uintptr_t addr, dsproto::Vec3& out) const;
    bool readMat3(uintptr_t addr, float out[9]) const;
};

}  // namespace dscoop
