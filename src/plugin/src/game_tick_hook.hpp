#pragma once
// Hook on the engine's per-frame update ("Game_Tick" signature). Everything that calls engine
// functions (puppets, later cargo/structures) runs from here on the game thread. Until recon
// provides the signature, isInstalled() is false and the puppet system stays idle.
#include <functional>
#include <string>

#include "dsloader/plugin_api.h"

namespace dscoop::gametick {

/// `callback(dtSec)` runs after the original tick. Returns false (with reason) if the signature is unresolved.
bool install(const DSLoaderApi& api, std::function<void(float dtSec)> callback, std::string* why);
void uninstall();
bool isInstalled();
unsigned tickCount();

}  // namespace dscoop::gametick
