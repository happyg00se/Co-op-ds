// DSLoader entry point. We are loaded as version.dll from the game folder; the real one is
// loaded from System32 and every export is forwarded to it (proxy_version.cpp).
#include <windows.h>

#include "loader.hpp"
#include "proxy_version.hpp"

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
