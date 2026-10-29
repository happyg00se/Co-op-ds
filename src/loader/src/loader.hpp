#pragma once
// DSLoader orchestration: config -> log -> crash handler -> wait for game -> module info ->
// signatures -> save backup -> (RTTI dump) -> plugins. Everything runs on a worker thread
// started from DllMain so the game keeps booting while we wait.
#include <atomic>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "dsloader/plugin_api.h"
#include "game_module.hpp"
#include "offsets.hpp"
#include "plugins.hpp"

namespace dsloader {

struct LoaderConfig {
    std::string logLevel = "info";
    uint32_t waitForWindowMs = 120000;   // give up waiting for the game window after this
    uint32_t waitExtraMs = 5000;         // after the window shows up, let the engine finish booting
    bool backupSaves = true;
    std::vector<std::string> saveDirs;   // extra save folders
    uint32_t keepBackups = 10;
    bool dumpRtti = false;
    bool rttiForceUnverified = false;
    bool loadPluginsWithoutRequiredSigs = false;
    bool chainLoad = true;              // load dscoop/chain/*.dll (another mod's version.dll moved aside by the installer)
    std::string pluginsDir = "plugins";

    static LoaderConfig fromJson(const nlohmann::json& j);
    static nlohmann::json defaults();
};

class Loader {
public:
    static Loader& instance();

    /// Called from DllMain(PROCESS_ATTACH). Starts the worker thread.
    void start(void* selfModule);
    /// Called from DllMain(PROCESS_DETACH).
    void stop();

    const GameModule& game() const { return game_; }
    const Offsets& offsets() const { return offsets_; }
    const LoaderConfig& config() const { return cfg_; }
    void* gameWindow() const { return gameWindow_.load(); }
    std::string collectReport();

private:
    void run();
    bool waitForGame();
    void buildApi();
    void loadChain();

    LoaderConfig cfg_;
    GameModule game_;
    Offsets offsets_;
    PluginHost plugins_;
    DSLoaderApi api_{};
    std::string rootUtf8_;
    std::string lastReportPath_;
    std::atomic<void*> gameWindow_{nullptr};
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    void* selfModule_ = nullptr;
};

}  // namespace dsloader
