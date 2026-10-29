#pragma once
// CoopManager — glue between the game (GameBridge), the network (dsnet::Session) and the UI.
// Lives for the lifetime of the plugin; update() is driven from the render thread (Present hook)
// so game memory is read while the engine is between frames.
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "dsloader/plugin_api.h"
#include "dsnet/session.hpp"
#include "game_bridge.hpp"
#include "puppets.hpp"
#include <mutex>

namespace dscoop {

struct CoopConfig {
    std::string nick = "Porter";
    std::string relayHost;                // empty = direct only
    uint16_t relayControlPort = 27000;
    uint16_t hostPort = 27015;
    std::string directHost;               // last used "ip:port" for direct join
    std::string lastCode;
    std::string menuKey = "F9";
    uint32_t tickRate = 30;
    bool pvp = false;
    bool dcContent = true;
    bool syncWeather = true;
    bool showMarkers = true;
    bool puppets = true;                  // spawn real Sams for remote players (stage 2)
    bool showRadar = true;
    bool autoHost = false;
    std::string autoJoinCode;
    std::string logLevel = "info";

    static nlohmann::json defaults();
    static CoopConfig fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

class CoopManager {
public:
    bool init(const DSLoaderApi& api, std::string* err);
    void shutdown();

    /// Per-frame: poll game, pump network, update stats. Render thread.
    void update();

    // UI actions
    void hostSession(bool useRelay);
    void joinByCode(const std::string& code);
    void joinDirect(const std::string& hostPort);
    void leaveSession();
    void sendChat(const std::string& text);
    void saveConfig();
    std::string collectReport();

    CoopConfig& config() { return cfg_; }
    dsnet::Session* session() { return session_.get(); }
    GameBridge& bridge() { return bridge_; }
    const DSLoaderApi& api() const { return *api_; }
    uint32_t nowMs() const;
    const std::deque<std::string>& chatLog() const { return chat_; }
    const std::deque<std::string>& eventLog() const { return events_; }
    void logEvent(const std::string& s);
    PuppetManager& puppets() { return puppets_; }
    bool gameTickHooked() const { return tickHooked_; }
    const std::string& gameTickStatus() const { return tickStatus_; }
    /// Serialises session access between the render thread (update/UI) and the game-tick thread (puppets).
    std::recursive_mutex& mutex() { return mutex_; }

private:
    void onGameTick(float dtSec);

    dsnet::SessionConfig sessionConfig() const;
    void applyLocalState(uint32_t now);

    const DSLoaderApi* api_ = nullptr;
    CoopConfig cfg_;
    nlohmann::json offsets_;
    GameBridge bridge_;
    std::unique_ptr<dsnet::Session> session_;
    std::deque<std::string> chat_;
    std::deque<std::string> events_;
    uint32_t lastFakeMs_ = 0;
    float fakeAngle_ = 0.0f;
    bool autoStarted_ = false;
    uint32_t startedMs_ = 0;
    PuppetManager puppets_;
    bool tickHooked_ = false;
    std::string tickStatus_;
    std::recursive_mutex mutex_;
};

}  // namespace dscoop
