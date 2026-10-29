// Forwarders for all documented version.dll exports (winver.h). Each stub resolves the real
// function lazily and tail-calls it. Stubs are named ds_<Export> and renamed to the public
// export name in version.def ("VerQueryValueA = ds_VerQueryValueA"), so they never clash with
// the SDK's own declarations (MinGW and the Windows SDK disagree on const-ness) and no assembly
// thunks are needed.
#include "proxy_version.hpp"

#include <windows.h>

namespace dsloader::proxy {
namespace {
HMODULE g_real = nullptr;

template <class Fn>
Fn load(const char* name) {
    if (!g_real) loadReal();
    return g_real ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(g_real, name))) : nullptr;
}
}  // namespace

bool loadReal() {
    if (g_real) return true;
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 13 >= MAX_PATH) return false;
    lstrcatW(path, L"\\version.dll");
    g_real = LoadLibraryW(path);
    return g_real != nullptr;
}

void unloadReal() {
    if (g_real) { FreeLibrary(g_real); g_real = nullptr; }
}
}  // namespace dsloader::proxy

using namespace dsloader::proxy;

#define DS_FORWARD(ret, name, params, args)                                   \
    extern "C" ret WINAPI ds_##name params {                                   \
        using Fn = ret(WINAPI*) params;                                        \
        static Fn s_real = load<Fn>(#name);                                    \
        if (!s_real) { SetLastError(ERROR_PROC_NOT_FOUND); return (ret)0; }   \
        return s_real args;                                                    \
    }

DS_FORWARD(BOOL, GetFileVersionInfoA, (LPCSTR f, DWORD h, DWORD len, LPVOID data), (f, h, len, data))
DS_FORWARD(BOOL, GetFileVersionInfoW, (LPCWSTR f, DWORD h, DWORD len, LPVOID data), (f, h, len, data))
DS_FORWARD(BOOL, GetFileVersionInfoExA, (DWORD flags, LPCSTR f, DWORD h, DWORD len, LPVOID data), (flags, f, h, len, data))
DS_FORWARD(BOOL, GetFileVersionInfoExW, (DWORD flags, LPCWSTR f, DWORD h, DWORD len, LPVOID data), (flags, f, h, len, data))
DS_FORWARD(DWORD, GetFileVersionInfoSizeA, (LPCSTR f, LPDWORD h), (f, h))
DS_FORWARD(DWORD, GetFileVersionInfoSizeW, (LPCWSTR f, LPDWORD h), (f, h))
DS_FORWARD(DWORD, GetFileVersionInfoSizeExA, (DWORD flags, LPCSTR f, LPDWORD h), (flags, f, h))
DS_FORWARD(DWORD, GetFileVersionInfoSizeExW, (DWORD flags, LPCWSTR f, LPDWORD h), (flags, f, h))
DS_FORWARD(DWORD, VerFindFileA, (DWORD flags, LPCSTR file, LPCSTR winDir, LPCSTR appDir, LPSTR curDir, PUINT curLen, LPSTR destDir, PUINT destLen),
           (flags, file, winDir, appDir, curDir, curLen, destDir, destLen))
DS_FORWARD(DWORD, VerFindFileW, (DWORD flags, LPCWSTR file, LPCWSTR winDir, LPCWSTR appDir, LPWSTR curDir, PUINT curLen, LPWSTR destDir, PUINT destLen),
           (flags, file, winDir, appDir, curDir, curLen, destDir, destLen))
DS_FORWARD(DWORD, VerInstallFileA, (DWORD flags, LPCSTR src, LPCSTR dest, LPCSTR srcDir, LPCSTR destDir, LPCSTR curDir, LPSTR tmp, PUINT tmpLen),
           (flags, src, dest, srcDir, destDir, curDir, tmp, tmpLen))
DS_FORWARD(DWORD, VerInstallFileW, (DWORD flags, LPCWSTR src, LPCWSTR dest, LPCWSTR srcDir, LPCWSTR destDir, LPCWSTR curDir, LPWSTR tmp, PUINT tmpLen),
           (flags, src, dest, srcDir, destDir, curDir, tmp, tmpLen))
DS_FORWARD(DWORD, VerLanguageNameA, (DWORD lang, LPSTR out, DWORD size), (lang, out, size))
DS_FORWARD(DWORD, VerLanguageNameW, (DWORD lang, LPWSTR out, DWORD size), (lang, out, size))
DS_FORWARD(BOOL, VerQueryValueA, (LPCVOID block, LPCSTR sub, LPVOID* out, PUINT len), (block, sub, out, len))
DS_FORWARD(BOOL, VerQueryValueW, (LPCVOID block, LPCWSTR sub, LPVOID* out, PUINT len), (block, sub, out, len))
