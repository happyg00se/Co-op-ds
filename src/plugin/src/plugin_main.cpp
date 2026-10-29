// DSCoop.dll — the co-op plugin. Loaded by DSLoader from dscoop/plugins/.
#include <windows.h>

#include <memory>
#include <string>

#include "coop.hpp"
#include "dscommon/version.hpp"
#include "dsloader/plugin_api.h"
#if DSCOOP_WITH_IMGUI
#include "dx12_hook.hpp"
#include "overlay.hpp"
#endif

namespace {
std::unique_ptr<dscoop::CoopManager> g_coop;
#if DSCOOP_WITH_IMGUI
std::unique_ptr<dscoop::Overlay> g_overlay;
#endif
const DSLoaderApi* g_api = nullptr;

void apiLog(DSLogLevel l, const std::string& s) { if (g_api) g_api->log(l, "DSCoop", s.c_str()); }
}  // namespace

extern "C" {

DSPLUGIN_EXPORT int DSPlugin_Query(DSPluginInfo* info) {
    if (!info || info->struct_size < sizeof(DSPluginInfo)) return 0;
    info->api_version = DSLOADER_API_VERSION;
    info->name = "DSCoop";
    info->version = DSCOOP_VERSION_STRING;
    return 1;
}

DSPLUGIN_EXPORT int DSPlugin_Load(const DSLoaderApi* api) {
    if (!api || api->api_version != DSLOADER_API_VERSION) return 0;
    g_api = api;
    g_coop = std::make_unique<dscoop::CoopManager>();
    std::string err;
    if (!g_coop->init(*api, &err)) { apiLog(DSLOG_ERROR, "init failed: " + err); g_coop.reset(); return 0; }
#if DSCOOP_WITH_IMGUI
    g_overlay = std::make_unique<dscoop::Overlay>(*g_coop);
    dscoop::dx12::Callbacks cb;
    cb.drawUi = [] { if (g_coop) { g_coop->update(); g_overlay->draw(); } };
    cb.wndProc = [](void* hwnd, unsigned msg, uintptr_t wp, intptr_t lp) { return g_overlay ? g_overlay->handleWndProc(hwnd, msg, wp, lp) : false; };
    if (!dscoop::dx12::install(*api, cb, &err)) {
        apiLog(DSLOG_ERROR, "DX12 overlay not available: " + err + " — network still runs via auto_host/auto_join_code in dscoop.json");
        // Without the Present hook nothing drives update(): fall back to a timer thread.
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            while (g_coop) { g_coop->update(); Sleep(16); }
            return 0;
        }, nullptr, 0, nullptr);
    }
#else
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        while (g_coop) { g_coop->update(); Sleep(16); }
        return 0;
    }, nullptr, 0, nullptr);
#endif
    apiLog(DSLOG_INFO, std::string("DSCoop ") + DSCOOP_VERSION_STRING + " loaded");
    return 1;
}

DSPLUGIN_EXPORT void DSPlugin_Unload(void) {
#if DSCOOP_WITH_IMGUI
    dscoop::dx12::uninstall();
    g_overlay.reset();
#endif
    if (g_coop) { g_coop->shutdown(); g_coop.reset(); }
    g_api = nullptr;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}
