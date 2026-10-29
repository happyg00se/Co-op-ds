#pragma once
#include <string>
#include <vector>

#include "dsloader/plugin_api.h"

namespace dsloader {

struct LoadedPlugin {
    std::string path;
    std::string name;
    std::string version;
    void* module = nullptr;  // HMODULE
    DSPlugin_UnloadFn unload = nullptr;
    bool loaded = false;
    std::string error;
};

class PluginHost {
public:
    /// Load every *.dll in `dir`. `api` must outlive the plugins.
    void loadAll(const std::string& dir, const DSLoaderApi& api);
    void unloadAll();
    const std::vector<LoadedPlugin>& plugins() const { return plugins_; }

private:
    std::vector<LoadedPlugin> plugins_;
};

}  // namespace dsloader
