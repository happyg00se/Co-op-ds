#include "game_tick_hook.hpp"

#include <atomic>
#include <chrono>

namespace dscoop::gametick {
namespace {
// Assumed shape (recon step 7): `void __fastcall Game_Tick(void* self, float dt)`; if the real
// function has another signature the detour keeps `this`/dt handling data-light: we only read dt
// when offsets.json says it is passed ("game_tick_has_dt": true), otherwise we measure it ourselves.
using TickFn = void (*)(void* self, float dt);
const DSLoaderApi* g_api = nullptr;
TickFn g_orig = nullptr;
void* g_target = nullptr;
std::function<void(float)> g_cb;
std::atomic<unsigned> g_count{0};
std::chrono::steady_clock::time_point g_last;

void hkTick(void* self, float dt) {
    g_orig(self, dt);
    const auto now = std::chrono::steady_clock::now();
    float measured = std::chrono::duration<float>(now - g_last).count();
    g_last = now;
    if (measured <= 0.0f || measured > 0.5f) measured = 1.0f / 60.0f;
    g_count++;
    if (g_cb) g_cb(measured);
}
}  // namespace

bool install(const DSLoaderApi& api, std::function<void(float)> callback, std::string* why) {
    g_api = &api;
    g_cb = std::move(callback);
    g_target = api.resolve("Game_Tick");
    if (!g_target) { if (why) *why = "signature Game_Tick not resolved (recon step 7)"; return false; }
    int st = api.hook_create(g_target, reinterpret_cast<void*>(&hkTick), reinterpret_cast<void**>(&g_orig));
    if (st != 0) { if (why) *why = "hook_create(Game_Tick) failed: " + std::to_string(st); return false; }
    g_last = std::chrono::steady_clock::now();
    api.hook_enable(g_target);
    api.log(DSLOG_INFO, "DSCoop/tick", "game tick hook installed");
    return true;
}

void uninstall() {
    if (g_api && g_target) { g_api->hook_disable(g_target); g_api->hook_remove(g_target); }
    g_target = nullptr;
    g_cb = nullptr;
}

bool isInstalled() { return g_target != nullptr; }
unsigned tickCount() { return g_count; }

}  // namespace dscoop::gametick
