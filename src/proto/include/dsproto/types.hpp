#pragma once
#include <cmath>
#include <cstdint>

namespace dsproto {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Vec3&) const = default;
    float length() const { return std::sqrt(x * x + y * y + z * z); }
};
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

/// Player state as replicated to peers. Positions are in Decima world units (metres),
/// velocity in m/s, angles in radians. `tick` is the host tick when the host emitted it,
/// or the guest's local frame counter when sent guest->host.
struct PlayerState {
    uint32_t tick = 0;
    Vec3 pos{};
    float yaw = 0.0f;    // heading, radians [-pi, pi)
    float pitch = 0.0f;  // camera/aim pitch, radians
    Vec3 vel{};
    uint16_t animState = 0;  // opaque for now: filled by GameBridge once animation graph ids are reverse-engineered
    uint16_t flags = 0;      // PlayerFlags bitmask
    float health = 1.0f;     // [0,1]
    float stamina = 1.0f;    // [0,1]
    bool operator==(const PlayerState&) const = default;
};

enum PlayerFlags : uint16_t {
    PF_Grounded  = 1 << 0,
    PF_Sprinting = 1 << 1,
    PF_Crouched  = 1 << 2,
    PF_InVehicle = 1 << 3,
    PF_Downed    = 1 << 4,
    PF_InCutscene = 1 << 5,
    PF_InMenu    = 1 << 6,
    PF_Fake      = 1 << 15,  // state produced by the test client / no game attached
};

}  // namespace dsproto
