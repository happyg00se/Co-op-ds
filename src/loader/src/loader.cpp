#include "loader.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "crash_handler.hpp"
#include "dscommon/config.hpp"
#include "dscommon/log.hpp"
#include "dscommon/paths.hpp"
#include "dscommon/version.hpp"
#include "hooks.hpp"
#include "rtti_dump.hpp"
#include "save_backup.hpp"

namespace dsloader {

nlohmann::json LoaderConfig::defaults() {
    return {
        {"_comment", "DSLoader settings. log_level: trace|debug|info|warn|error. See docs/architecture.md."},
        {"log_level", "info"},
        {"wait_for_window_ms", 120000},
        {"wait_extra_ms", 5000},
        {"backup_saves", true},
        {"save_dirs", nlohmann::json::array()},
        {"keep_backups", 10},
        {"dump_rtti", false},
        {"rtti_force_unverified", false},
        {"load_plugins_without_required_sigs", false},
        {"plugins_dir", "plugins"},
    };
}

LoaderConfig LoaderConfig::fromJson(const nlohmann::json& j) {
    LoaderConfig c;
    c.logLevel = j.value("log_level", c.logLevel);
    c.waitForWindowMs = j.value("wait_for_window_ms", c.waitForWindowMs);
    c.waitExtraMs = j.value("wait_extra_ms", c.waitExtraMs);
    c.backupSaves = j.value("backup_saves", c.backupSaves);
    if (j.contains("save_dirs") && j["save_dirs"].is_array())
        for (const auto& s : j["save_dirs"]) if (s.is_string()) c.saveDirs.push_back(s.get<std::string>());
    c.keepBackups = j.value("keep_backups", c.keepBackups);
    c.dumpRtti = j.value("dump_rtti", c.dumpRtti);
    c.rttiForceUnverified = j.value("rtti_force_unverified", c.rttiForceUnverified);
    c.loadPluginsWithoutRequiredSigs = j.value("load_plugins_without_required_sigs", c.loadPluginsWithoutRequiredSigs);
    c.pluginsDir = j.value("plugins_dir", c.pluginsDir);
    return c;
}

Loader& Loader::instance() {
    static Loader l;
    return l;
}

void Loader::start(void* selfModule) {
    selfModule_ = selfModule;
    worker_ = std::thread([this] { run(); });
}

void Loader::stop() {
    stopping_ = true;
    if (worker_.joinable()) worker_.detach();  // never block the loader lock in DllMain
    plugins_.unloadAll();
    Hooks::instance().shutdown();
    DSLOG_INFO("DSLoader shutting down");
    dscommon::log::shutdown();
}

// ---------------------------------------------------------------------------
namespace {
void apiLog(DSLogLevel level, const char* plugin, const char* message) {
    auto lg = dscommon::log::get();
    const char* p = plugin ? plugin : "plugin";
    switch (level) {
        case DSLOG_TRACE: lg->trace("[{}] {}", p, message); break;
        case DSLOG_DEBUG: lg->debug("[{}] {}", p, message); break;
        case DSLOG_INFO: lg->info("[{}] {}", p, message); break;
        case DSLOG_WARN: lg->warn("[{}] {}", p, message); break;
        default: lg->error("[{}] {}", p, message); break;
    }
}
void* apiResolve(const char* name) { return reinterpret_cast<void*>(Loader::instance().offsets().resolve(name ? name : "")); }
const char* apiOffsetsJson() { return Loader::instance().offsets().rawText().c_str(); }
int apiHookCreate(void* t, void* d, void** o) { return Hooks::instance().create(t, d, o); }
int apiHookEnable(void* t) { return Hooks::instance().enable(t); }
int apiHookDisable(void* t) { return Hooks::instance().disable(t); }
int apiHookRemove(void* t) { return Hooks::instance().remove(t); }
void* apiGameWindow() { return Loader::instance().gameWindow(); }
int apiSafeRead(const void* src, void* dst, size_t n) { return safeRead(src, dst, n) ? 1 : 0; }
std::string g_reportPath;
const char* apiCollectReport() {
    g_reportPath = Loader::instance().collectReport();
    return g_reportPath.empty() ? nullptr : g_reportPath.c_str();
}
}  // namespace

void Loader::buildApi() {
    api_ = DSLoaderApi{};
    api_.struct_size = sizeof(api_);
    api_.api_version = DSLOADER_API_VERSION;
    api_.loader_version = DSCOOP_VERSION_STRING;
    api_.root_dir_utf8 = rootUtf8_.c_str();
    api_.game = &game_.api();
    api_.log = &apiLog;
    api_.resolve = &apiResolve;
    api_.offsets_json = &apiOffsetsJson;
    api_.hook_create = &apiHookCreate;
    api_.hook_enable = &apiHookEnable;
    api_.hook_disable = &apiHookDisable;
    api_.hook_remove = &apiHookRemove;
    api_.game_window = &apiGameWindow;
    api_.safe_read = &apiSafeRead;
    api_.collect_report = &apiCollectReport;
}

bool Loader::waitForGame() {
    const auto start = std::chrono::steady_clock::now();
    while (!stopping_) {
        if (void* w = GameModule::findMainWindow()) {
            gameWindow_ = w;
            DSLOG_INFO("game window found after {} ms, waiting {} ms more for the engine",
                       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(), cfg_.waitExtraMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.waitExtraMs));
            return true;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(cfg_.waitForWindowMs)) {
            DSLOG_WARN("no game window after {} ms — continuing anyway", cfg_.waitForWindowMs);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

void Loader::run() {
    using namespace dscommon;
    // version.dll lives next to the exe, so <exe dir>/dscoop is our root (unless DSCOOP_ROOT is set).
    paths::setRoot(paths::moduleDir(selfModule_) / "dscoop");
    paths::ensureTree();
    rootUtf8_ = paths::root().string();

    const auto cfgJson = config::loadOrCreate(paths::loaderConfig(), LoaderConfig::defaults());
    cfg_ = LoaderConfig::fromJson(cfgJson);

    log::Options lo;
    lo.file = paths::logs() / "dsloader.log";
    lo.name = "dsloader";
    lo.level = log::parseLevel(cfg_.logLevel);
    log::init(lo);
    DSLOG_INFO("DSLoader v{} starting, root {}", DSCOOP_VERSION_STRING, rootUtf8_);
    installCrashHandler(paths::reports().string());

    std::string err;
    if (!game_.init(&err)) { DSLOG_ERROR("game module: {}", err); return; }
    DSLOG_INFO("game exe: {}", game_.exePath());
    DSLOG_INFO("game base {:#x} size {:#x} timestamp {:#x} build_id {}", game_.base(), game_.size(), game_.api().timestamp, game_.buildId());
    for (const auto& s : game_.sections()) DSLOG_DEBUG("  section {:8} {:#x} +{:#x}{}", s.name, s.begin, s.size, s.executable() ? " X" : "");

    if (cfg_.backupSaves) {
        auto r = backupSaves(defaultSaveDirs(cfg_.saveDirs), paths::backups(), cfg_.keepBackups);
        if (!r.ok) DSLOG_WARN("saves: backup skipped: {}", r.error);
    }

    if (!waitForGame()) return;

    if (!Hooks::instance().init(&err)) { DSLOG_ERROR("MinHook init failed: {}", err); return; }

    const auto offsetsPath = paths::offsetsJson().string();
    if (!offsets_.load(offsetsPath, &err)) {
        DSLOG_ERROR("offsets: {}", err);
    } else {
        if (!err.empty()) DSLOG_WARN("offsets: {}", err);
        const bool allRequired = offsets_.resolveAll(game_, paths::cache().string());
        for (const auto& r : offsets_.results()) {
            if (r.address) DSLOG_INFO("sig {:40} -> {:#x} (rva {:#x}){}", r.name, r.address, r.address - game_.base(), r.fromCache ? " [cache]" : "");
            else if (r.required) DSLOG_ERROR("sig {:40} -> MISSING (required): {}", r.name, r.error);
            else DSLOG_WARN("sig {:40} -> missing: {}", r.name, r.error);
        }
        DSLOG_INFO("offsets: {}/{} resolved, {} pending recon", offsets_.resolvedCount(), offsets_.results().size(), offsets_.pendingCount());
        if (!allRequired && !cfg_.loadPluginsWithoutRequiredSigs) {
            DSLOG_ERROR("required signatures missing — plugins are NOT loaded (set load_plugins_without_required_sigs=true to override)");
            return;
        }
    }

    if (cfg_.dumpRtti) {
        auto r = dumpRtti(game_, offsets_.json(), paths::dumps().string(), cfg_.rttiForceUnverified);
        if (!r.ok) DSLOG_WARN("rtti: {}", r.error);
    }

    buildApi();
    plugins_.loadAll((paths::root() / cfg_.pluginsDir).string(), api_);
    DSLOG_INFO("DSLoader ready: {} plugin(s) loaded", std::count_if(plugins_.plugins().begin(), plugins_.plugins().end(), [](const LoadedPlugin& p) { return p.loaded; }));
}

std::string Loader::collectReport() {
    using namespace dscommon;
    std::error_code ec;
    const auto dir = paths::reports() / ("report_" + paths::timestampForFiles());
    std::filesystem::create_directories(dir, ec);
    log::get()->flush();
    auto copyTree = [&](const std::filesystem::path& from, const char* name) {
        if (std::filesystem::exists(from, ec)) std::filesystem::copy(from, dir / name, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    };
    copyTree(paths::logs(), "logs");
    copyTree(paths::data(), "data");
    copyTree(paths::dumps(), "dumps");
    copyTree(paths::loaderConfig(), "dsloader.json");
    copyTree(paths::pluginConfig(), "dscoop.json");
    nlohmann::json info;
    info["dscoop_version"] = DSCOOP_VERSION_STRING;
    info["game_exe"] = game_.exePath();
    info["game_build_id"] = game_.buildId();
    info["game_timestamp"] = game_.api().timestamp;
    info["plugins"] = nlohmann::json::array();
    for (const auto& p : plugins_.plugins()) info["plugins"].push_back({{"name", p.name}, {"version", p.version}, {"loaded", p.loaded}, {"error", p.error}});
    info["signatures"] = nlohmann::json::array();
    for (const auto& r : offsets_.results()) info["signatures"].push_back({{"name", r.name}, {"rva", r.address ? r.address - game_.base() : 0}, {"error", r.error}});
    info["hooks"] = nlohmann::json::array();
    for (const auto& [addr, name] : Hooks::instance().list()) info["hooks"].push_back({{"name", name}, {"address", reinterpret_cast<uintptr_t>(addr)}});
    std::ofstream out(dir / "report.json");
    out << info.dump(2);
    DSLOG_INFO("report collected: {}", dir.string());
    return dir.string();
}

}  // namespace dsloader
