#pragma once
// Quantization helpers for compact on-wire state.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace dsproto::quant {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

/// Angle in radians [-pi, pi) -> u16 full circle. Resolution ~0.0055 degrees.
inline uint16_t angleToU16(float rad) {
    float t = std::fmod(rad, kTwoPi);
    if (t < 0) t += kTwoPi;
    return static_cast<uint16_t>(std::lround(t / kTwoPi * 65535.0f) & 0xFFFF);
}
inline float u16ToAngle(uint16_t q) {
    float t = static_cast<float>(q) / 65535.0f * kTwoPi;
    if (t >= std::numbers::pi_v<float>) t -= kTwoPi;
    return t;
}

/// Signed float with a fixed scale -> i16 with saturation.
inline int16_t toI16(float v, float scale) {
    const float s = std::clamp(v * scale, -32767.0f, 32767.0f);
    return static_cast<int16_t>(std::lround(s));
}
inline float fromI16(int16_t q, float scale) { return static_cast<float>(q) / scale; }

/// [0,1] -> u8
inline uint8_t unitToU8(float v) {
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}
inline float u8ToUnit(uint8_t q) { return static_cast<float>(q) / 255.0f; }

/// Shortest-arc interpolation between two angles in radians.
inline float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a, kTwoPi);
    if (d > std::numbers::pi_v<float>) d -= kTwoPi;
    if (d < -std::numbers::pi_v<float>) d += kTwoPi;
    float r = a + d * t;
    if (r >= std::numbers::pi_v<float>) r -= kTwoPi;
    if (r < -std::numbers::pi_v<float>) r += kTwoPi;
    return r;
}

}  // namespace dsproto::quant
