#include "hooks.hpp"

#include <MinHook.h>

namespace dsloader {

Hooks& Hooks::instance() {
    static Hooks h;
    return h;
}

bool Hooks::init(std::string* err) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (initialized_) return true;
    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        if (err) *err = MH_StatusToString(st);
        return false;
    }
    initialized_ = true;
    return true;
}

void Hooks::shutdown() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!initialized_) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    names_.clear();
    initialized_ = false;
}

int Hooks::create(void* target, void* detour, void** original, const std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!initialized_) return MH_ERROR_NOT_INITIALIZED;
    const MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st == MH_OK) names_[target] = name.empty() ? "hook" : name;
    return st;
}
int Hooks::enable(void* target) { return MH_EnableHook(target); }
int Hooks::disable(void* target) { return MH_DisableHook(target); }
int Hooks::remove(void* target) {
    std::lock_guard<std::mutex> lk(mutex_);
    const MH_STATUS st = MH_RemoveHook(target);
    if (st == MH_OK) names_.erase(target);
    return st;
}
const char* Hooks::statusText(int status) { return MH_StatusToString(static_cast<MH_STATUS>(status)); }

std::map<void*, std::string> Hooks::list() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return names_;
}

}  // namespace dsloader
