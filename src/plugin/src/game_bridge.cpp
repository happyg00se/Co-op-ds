#include "game_bridge.hpp"

#include <cmath>

namespace dscoop {

void GameBridge::init(const nlohmann::json& offsets, uintptr_t moduleBase, Resolver resolver, Reader reader) {
    moduleBase_ = moduleBase;
    resolver_ = std::move(resolver);
    reader_ = std::move(reader);
    chains_.clear();
    if (offsets.contains("pointers") && offsets["pointers"].is_object()) {
        for (auto it = offsets["pointers"].begin(); it != offsets["pointers"].end(); ++it) {
            const auto& v = it.value();
            if (!v.is_object()) continue;
            Chain c;
            c.base = v.value("base", "module");
            c.add = v.value("add", 0ll);
            if (v.contains("chain") && v["chain"].is_array()) for (const auto& o : v["chain"]) if (o.is_number()) c.chain.push_back(o.get<int64_t>());
            c.type = v.value("type", "u64");
            c.note = v.value("note", "");
            chains_[it.key()] = std::move(c);
        }
        if (offsets["pointers"].contains("camera_view_proj")) matLayout_ = offsets["pointers"]["camera_view_proj"].value("layout", "row_major");
        if (offsets["pointers"].contains("camera_rotation")) {
            const auto& cr = offsets["pointers"]["camera_rotation"];
            rightRow_ = cr.value("right_row", 0); forwardRow_ = cr.value("forward_row", 1); upRow_ = cr.value("up_row", 2);
        }
    }
    status_ = chains_.count("local_player_position") ? "pointers loaded, not yet validated" : "no local_player_position pointer in offsets.json (recon step 3)";
}

std::optional<uintptr_t> GameBridge::baseAddress(const std::string& base) const {
    if (base == "module") return moduleBase_;
    if (base.rfind("sig:", 0) == 0) {
        const uintptr_t a = resolver_ ? resolver_(base.substr(4)) : 0;
        if (!a) return std::nullopt;
        return a;
    }
    if (base.rfind("ptr:", 0) == 0) {
        auto it = chains_.find(base.substr(4));
        if (it == chains_.end()) return std::nullopt;
        return follow(it->second);
    }
    return std::nullopt;
}

std::optional<uintptr_t> GameBridge::follow(const Chain& c) const {
    auto base = baseAddress(c.base);
    if (!base) return std::nullopt;
    uintptr_t addr = *base + static_cast<uintptr_t>(c.add);
    for (int64_t off : c.chain) {
        uint64_t p = 0;
        if (!reader_(reinterpret_cast<const void*>(addr), &p, sizeof(p)) || p == 0) return std::nullopt;
        addr = static_cast<uintptr_t>(p) + static_cast<uintptr_t>(off);
    }
    return addr;
}

void GameBridge::update() {
    ++frame_;
    attached_ = false;
    auto posIt = chains_.find("local_player_position");
    if (posIt == chains_.end() || !reader_) return;
    auto addr = follow(posIt->second);
    if (!addr) { status_ = "position chain does not resolve (game not in world, or offsets wrong)"; return; }
    dsproto::Vec3 pos;
    if (!readVec3(posIt->second, *addr, pos)) { status_ = "position unreadable"; return; }
    // Sanity: Decima worlds are a few km across. Garbage pointers produce astronomic values.
    if (std::fabs(pos.x) > 1e6f || std::fabs(pos.y) > 1e6f || std::fabs(pos.z) > 1e6f) { status_ = "position out of range (bad chain)"; return; }

    dsproto::PlayerState s = local_;
    s.tick = frame_;
    s.vel = (pos - local_.pos) * 60.0f;  // rough; replaced by the engine's velocity once found
    if (s.vel.length() > 100.0f) s.vel = {};
    s.pos = pos;
    s.flags &= static_cast<uint16_t>(~dsproto::PF_Fake);

    if (auto rot = chains_.find("local_player_yaw"); rot != chains_.end()) {
        if (auto ra = follow(rot->second)) {
            if (rot->second.type == "quat_f32") {
                float q[4];
                if (reader_(reinterpret_cast<const void*>(*ra), q, sizeof(q))) {
                    // yaw around Z (Decima is Z-up): atan2(2(wz+xy), 1-2(y^2+z^2))
                    s.yaw = std::atan2(2.0f * (q[3] * q[2] + q[0] * q[1]), 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]));
                }
            } else if (rot->second.type == "mat3_f32") {
                float m[9];
                // Decima WorldTransform keeps a 3x3 float orientation right before the double position.
                if (readMat3(*ra, m)) s.yaw = std::atan2(m[forwardRow_ * 3 + 1], m[forwardRow_ * 3 + 0]);
            } else {
                float y = 0;
                if (reader_(reinterpret_cast<const void*>(*ra), &y, sizeof(y)) && std::isfinite(y)) s.yaw = y;
            }
        }
    }
    if (auto hp = chains_.find("local_player_health"); hp != chains_.end()) {
        if (auto ha = follow(hp->second)) {
            float h = 0;
            if (reader_(reinterpret_cast<const void*>(*ha), &h, sizeof(h)) && std::isfinite(h)) s.health = h > 1.0f ? h / 1000.0f : h;
        }
    }
    local_ = s;
    attached_ = true;
    status_ = "attached";

    viewProj_.reset();
    camPos_.reset();
    if (auto cam = chains_.find("camera_view_proj"); cam != chains_.end()) {
        if (auto ca = follow(cam->second)) {
            Mat4 m;
            if (reader_(reinterpret_cast<const void*>(*ca), m.m, sizeof(m.m))) {
                bool finite = true;
                for (float v : m.m) finite = finite && std::isfinite(v);
                if (finite) viewProj_ = m;
            }
        }
    }
    if (auto cp = chains_.find("camera_position"); cp != chains_.end()) {
        if (auto ca = follow(cp->second)) {
            dsproto::Vec3 c;
            if (readVec3(cp->second, *ca, c)) camPos_ = c;
        }
    }
    // No direct VP matrix: build one from camera orientation + position + vertical FOV.
    if (!viewProj_ && camPos_) {
        auto rotIt = chains_.find("camera_rotation");
        auto fovIt = chains_.find("camera_fov");
        float m[9];
        float fovDeg = 55.0f;
        bool haveRot = false;
        if (rotIt != chains_.end()) if (auto ra = follow(rotIt->second)) haveRot = readMat3(*ra, m);
        if (fovIt != chains_.end()) if (auto fa = follow(fovIt->second)) { float f; if (reader_(reinterpret_cast<const void*>(*fa), &f, 4) && std::isfinite(f) && f > 1.0f && f < 179.0f) fovDeg = f; }
        if (haveRot) {
            const float* r = m + rightRow_ * 3;
            const float* f = m + forwardRow_ * 3;
            const float* u = m + upRow_ * 3;
            const dsproto::Vec3& e = *camPos_;
            // View matrix (row-major, applied as M*v): rows = right, up, -forward (camera looks down -Z in clip space).
            float view[16] = {r[0], r[1], r[2], -(r[0] * e.x + r[1] * e.y + r[2] * e.z),
                              u[0], u[1], u[2], -(u[0] * e.x + u[1] * e.y + u[2] * e.z),
                              -f[0], -f[1], -f[2], (f[0] * e.x + f[1] * e.y + f[2] * e.z),
                              0, 0, 0, 1};
            const float fy = 1.0f / std::tan(fovDeg * 3.14159265f / 360.0f);
            const float fx = fy / aspect_;
            const float zn = 0.1f, zf = 5000.0f;
            float proj[16] = {fx, 0, 0, 0, 0, fy, 0, 0, 0, 0, zf / (zn - zf), zn * zf / (zn - zf), 0, 0, -1, 0};
            Mat4 vp{};
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) {
                    float acc = 0;
                    for (int k = 0; k < 4; ++k) acc += proj[i * 4 + k] * view[k * 4 + j];
                    vp.m[i * 4 + j] = acc;
                }
            viewProj_ = vp;
        }
    }
}

bool GameBridge::readVec3(const Chain& c, uintptr_t addr, dsproto::Vec3& out) const {
    if (c.type == "vec3d") {
        double d[3];
        if (!reader_(reinterpret_cast<const void*>(addr), d, sizeof(d))) return false;
        out = {static_cast<float>(d[0]), static_cast<float>(d[1]), static_cast<float>(d[2])};
    } else {
        float f[3];
        if (!reader_(reinterpret_cast<const void*>(addr), f, sizeof(f))) return false;
        out = {f[0], f[1], f[2]};
    }
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool GameBridge::readMat3(uintptr_t addr, float out[9]) const {
    if (!reader_(reinterpret_cast<const void*>(addr), out, sizeof(float) * 9)) return false;
    for (int i = 0; i < 9; ++i) if (!std::isfinite(out[i]) || std::fabs(out[i]) > 1.01f) return false;
    return true;
}

std::vector<GameBridge::PointerDebug> GameBridge::debugPointers() const {
    std::vector<PointerDebug> out;
    for (const auto& [name, c] : chains_) {
        auto a = follow(c);
        out.push_back({name, a.value_or(0), c.type, a.has_value(), c.note});
    }
    return out;
}

}  // namespace dscoop
