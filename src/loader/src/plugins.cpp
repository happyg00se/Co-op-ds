#include "plugins.hpp"

#include <windows.h>

#include <filesystem>

#include "dscommon/log.hpp"

namespace dsloader {

void PluginHost::loadAll(const std::string& dir, const DSLoaderApi& api) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        DSLOG_WARN("plugins: directory {} does not exist", dir);
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        if (ext != L".dll") continue;
        LoadedPlugin p;
        p.path = entry.path().string();
        HMODULE mod = LoadLibraryW(entry.path().wstring().c_str());
        if (!mod) {
            p.error = "LoadLibrary failed, error " + std::to_string(GetLastError());
            DSLOG_ERROR("plugins: {}: {}", p.path, p.error);
            plugins_.push_back(p);
            continue;
        }
        auto query = reinterpret_cast<DSPlugin_QueryFn>(reinterpret_cast<void*>(GetProcAddress(mod, "DSPlugin_Query")));
        auto load = reinterpret_cast<DSPlugin_LoadFn>(reinterpret_cast<void*>(GetProcAddress(mod, "DSPlugin_Load")));
        auto unload = reinterpret_cast<DSPlugin_UnloadFn>(reinterpret_cast<void*>(GetProcAddress(mod, "DSPlugin_Unload")));
        if (!query || !load) {
            p.error = "not a DSLoader plugin (missing DSPlugin_Query/DSPlugin_Load)";
            DSLOG_WARN("plugins: {}: {}", p.path, p.error);
            FreeLibrary(mod);
            plugins_.push_back(p);
            continue;
        }
        DSPluginInfo info{};
        info.struct_size = sizeof(info);
        if (!query(&info) || !info.name) {
            p.error = "DSPlugin_Query declined";
            FreeLibrary(mod);
            plugins_.push_back(p);
            continue;
        }
        p.name = info.name;
        p.version = info.version ? info.version : "?";
        if (info.api_version != DSLOADER_API_VERSION) {
            p.error = "plugin API " + std::to_string(info.api_version) + " != loader API " + std::to_string(DSLOADER_API_VERSION);
            DSLOG_ERROR("plugins: {} {}: {}", p.name, p.version, p.error);
            FreeLibrary(mod);
            plugins_.push_back(p);
            continue;
        }
        p.module = mod;
        p.unload = unload;
        DSLOG_INFO("plugins: loading {} v{} ({})", p.name, p.version, p.path);
        if (!load(&api)) {
            p.error = "DSPlugin_Load returned failure";
            DSLOG_ERROR("plugins: {}: {}", p.name, p.error);
            if (unload) unload();
            FreeLibrary(mod);
            p.module = nullptr;
        } else {
            p.loaded = true;
            DSLOG_INFO("plugins: {} loaded", p.name);
        }
        plugins_.push_back(p);
    }
}

void PluginHost::unloadAll() {
    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        if (!it->loaded) continue;
        if (it->unload) it->unload();
        if (it->module) FreeLibrary(static_cast<HMODULE>(it->module));
        it->loaded = false;
    }
}

}  // namespace dsloader
