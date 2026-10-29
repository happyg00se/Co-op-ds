// DSLoader entry point. We are loaded as version.dll from the game folder; the real one is
// loaded from System32 and every export is forwarded to it (proxy_version.cpp).
#include <windows.h>

#include "dscommon/version.hpp"
#include "loader.hpp"
#include "proxy_version.hpp"

// Marker export: the installer reads export names from disk to tell our version.dll apart from
// another mod's proxy DLL. Exported as "DSLoader_GetVersion" via version.def.
extern "C" const char* WINAPI ds_DSLoader_GetVersion() { return DSCOOP_VERSION_STRING; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            if (!dsloader::proxy::loadReal()) return FALSE;  // without the real DLL the game cannot run either
            dsloader::Loader::instance().start(instance);
            break;
        case DLL_PROCESS_DETACH:
            dsloader::Loader::instance().stop();
            dsloader::proxy::unloadReal();
            break;
        default: break;
    }
    return TRUE;
}
