#include "coop.hpp"

#include <cmath>

#include "dscommon/config.hpp"
#include "dscommon/log.hpp"
#include "dscommon/paths.hpp"
#include "dscommon/version.hpp"
#include "dsnet/enet_transport.hpp"

namespace dscoop {
using namespace dsproto;

nlohmann::json CoopConfig::defaults() { return CoopConfig{}.toJson(); }

nlohmann::json CoopConfig::toJson() const {
    return {
        {"_comment", "DSCoop user settings. relay.host empty = direct connections only. menu_key: F1..F12 or a letter."},
        {"nick", nick},
        {"relay", {{"host", relayHost}, {"control_port", relayControlPort}}},
        {"host_port", hostPort},
        {"direct_host", directHost},
        {"last_code", lastCode},
        {"menu_key", menuKey},
        {"tick_rate", tickRate},
        {"host_flags", {{"pvp", pvp}, {"dc_content", dcContent}, {"sync_weather", syncWeather}}},
        {"ui", {{"show_markers", showMarkers}, {"show_radar", showRadar}}},
        {"auto_host", autoHost},
        {"auto_join_code", autoJoinCode},
        {"log_level", logLevel},
    };
}

CoopConfig CoopConfig::fromJson(const nlohmann::json& j) {
    CoopConfig c;
    c.nick = j.value("nick", c.nick);
    if (j.contains("relay") && j["relay"].is_object()) {
        c.relayHost = j["relay"].value("host", c.relayHost);
        c.relayControlPort = j["relay"].value("control_port", c.relayControlPort);
    }
    c.hostPort = j.value("host_port", c.hostPort);
    c.directHost = j.value("direct_host", c.directHost);
    c.lastCode = j.value("last_code", c.lastCode);
    c.menuKey = j.value("menu_key", c.menuKey);
    c.tickRate = j.value("tick_rate", c.tickRate);
    if (j.contains("host_flags") && j["host_flags"].is_object()) {
        c.pvp = j["host_flags"].value("pvp", c.pvp);
        c.dcContent = j["host_flags"].value("dc_content", c.dcContent);
        c.syncWeather = j["host_flags"].value("sync_weather", c.syncWeather);
    }
    if (j.contains("ui") && j["ui"].is_object()) {
        c.showMarkers = j["ui"].value("show_markers", c.showMarkers);
        c.showRadar = j["ui"].value("show_radar", c.showRadar);
    }
    c.autoHost = j.value("auto_host", c.autoHost);
    c.autoJoinCode = j.value("auto_join_code", c.autoJoinCode);
    c.logLevel = j.value("log_level", c.logLevel);
    return c;
}

// ---------------------------------------------------------------------------
bool CoopManager::init(const DSLoaderApi& api, std::string* err) {
    api_ = &api;
    using namespace dscommon;
    paths::setRoot(api.root_dir_utf8);
    const auto j = config::loadOrCreate(paths::pluginConfig(), CoopConfig::defaults());
    cfg_ = CoopConfig::fromJson(j);

    log::Options lo;
    lo.file = paths::logs() / "dscoop.log";
    lo.name = "dscoop";
    lo.level = log::parseLevel(cfg_.logLevel);
    log::init(lo);
    DSLOG_INFO("DSCoop plugin v{} init (loader {}, game build {})", DSCOOP_VERSION_STRING, api.loader_version, api.game->build_id);

    try {
        offsets_ = nlohmann::json::parse(api.offsets_json() ? api.offsets_json() : "{}", nullptr, true, true);
    } catch (const std::exception& e) {
        DSLOG_WARN("offsets.json unreadable from plugin: {}", e.what());
        offsets_ = nlohmann::json::object();
    }
    bridge_.init(offsets_, reinterpret_cast<uintptr_t>(api.game->base),
                 [this](const std::string& n) { return reinterpret_cast<uintptr_t>(api_->resolve(n.c_str())); },
                 [this](const void* s, void* d, size_t n) { return api_->safe_read(s, d, n) != 0; });
    DSLOG_INFO("game bridge: {}", bridge_.status());

    session_ = std::make_unique<dsnet::Session>(std::make_unique<dsnet::EnetTransport>());
    session_->onLog = [this](const std::string& s) { DSLOG_INFO("{}", s); logEvent(s); };
    session_->onPlayerJoined = [this](const PlayerInfo& p) { logEvent("player joined: " + p.nick); };
    session_->onPlayerLeft = [this](uint8_t id, uint8_t) { logEvent("player left: #" + std::to_string(id)); };
    session_->onChat = [this](uint8_t from, const std::string& t) {
        std::string who = "#" + std::to_string(from);
        if (from == session_->localId()) who = cfg_.nick;
        else if (auto it = session_->players().find(from); it != session_->players().end()) who = it->second.nick;
        chat_.push_back(who + ": " + t);
        while (chat_.size() > 200) chat_.pop_front();
    };
    startedMs_ = nowMs();
    (void)err;
    return true;
}

void CoopManager::shutdown() {
    if (session_) { session_->leave(); session_.reset(); }
    saveConfig();
    DSLOG_INFO("DSCoop plugin shutdown");
    dscommon::log::shutdown();
}

uint32_t CoopManager::nowMs() const { return dsnet::nowMs(); }

void CoopManager::logEvent(const std::string& s) {
    events_.push_back(s);
    while (events_.size() > 100) events_.pop_front();
}

dsnet::SessionConfig CoopManager::sessionConfig() const {
    dsnet::SessionConfig sc;
    sc.nick = cfg_.nick;
    sc.clientVersion = DSCOOP_VERSION_INT;
    sc.gameBuild = api_->game->build_id;
    sc.features = bridge_.isAttached() ? CF_HasOffsets : CF_FakeClient;
    sc.tickRate = cfg_.tickRate;
    sc.hostFlags = (cfg_.pvp ? HF_PvP : 0u) | (cfg_.dcContent ? HF_DirectorsCutContent : 0u) | (cfg_.syncWeather ? HF_SyncWeather | HF_SyncTimeOfDay : 0u);
    return sc;
}

void CoopManager::hostSession(bool useRelay) {
    std::string err;
    std::optional<dsnet::RelayOptions> relay;
    if (useRelay) {
        if (cfg_.relayHost.empty()) { logEvent("relay host is empty in settings"); return; }
        relay = dsnet::RelayOptions{cfg_.relayHost, cfg_.relayControlPort};
    }
    if (!session_->host(sessionConfig(), cfg_.hostPort, relay, &err)) logEvent("host failed: " + err);
}

void CoopManager::joinByCode(const std::string& code) {
    if (cfg_.relayHost.empty()) { logEvent("relay host is empty in settings"); return; }
    std::string err;
    cfg_.lastCode = code;
    if (!session_->joinRelay(sessionConfig(), dsnet::RelayOptions{cfg_.relayHost, cfg_.relayControlPort}, code, &err)) logEvent("join failed: " + err);
}

void CoopManager::joinDirect(const std::string& hostPort) {
    auto ep = dsnet::Endpoint::parse(hostPort, 27015);
    if (!ep) { logEvent("bad address, use ip:port"); return; }
    cfg_.directHost = hostPort;
    std::string err;
    if (!session_->joinDirect(sessionConfig(), *ep, &err)) logEvent("join failed: " + err);
}

void CoopManager::leaveSession() { if (session_) session_->leave(); }
void CoopManager::sendChat(const std::string& text) { if (session_ && !text.empty()) session_->sendChat(text); }

void CoopManager::saveConfig() {
    std::string err;
    if (!dscommon::config::save(dscommon::paths::pluginConfig(), cfg_.toJson(), &err)) DSLOG_WARN("config save: {}", err);
}

std::string CoopManager::collectReport() {
    dscommon::log::get()->flush();
    const char* p = api_->collect_report();
    const std::string path = p ? p : "";
    logEvent(path.empty() ? "report failed" : "report saved: " + path);
    return path;
}

void CoopManager::applyLocalState(uint32_t now) {
    if (bridge_.isAttached()) {
        session_->setLocalState(bridge_.localState());
        return;
    }
    // No offsets yet: send a slow circle so peers can see "something" during stage-1 tests.
    if (now - lastFakeMs_ < 16) return;
    lastFakeMs_ = now;
    fakeAngle_ += 0.01f;
    PlayerState s;
    s.tick = now;
    s.pos = {10.0f * std::cos(fakeAngle_), 10.0f * std::sin(fakeAngle_), 0.0f};
    s.vel = {-6.0f * std::sin(fakeAngle_), 6.0f * std::cos(fakeAngle_), 0.0f};
    s.yaw = fakeAngle_ + 1.5708f;
    s.flags = PF_Fake | PF_Grounded;
    session_->setLocalState(s);
}

void CoopManager::update() {
    if (!session_) return;
    const uint32_t now = nowMs();
    bridge_.update();
    if (!autoStarted_ && now - startedMs_ > 3000) {
        autoStarted_ = true;
        if (cfg_.autoHost) hostSession(!cfg_.relayHost.empty());
        else if (!cfg_.autoJoinCode.empty()) joinByCode(cfg_.autoJoinCode);
    }
    if (session_->role() != dsnet::Session::Role::None) applyLocalState(now);
    session_->update(now);
}

}  // namespace dscoop
