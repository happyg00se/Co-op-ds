#include "dscommon/paths.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace dscommon::paths {
namespace {
std::filesystem::path g_root;
std::mutex g_mutex;
}  // namespace

std::filesystem::path exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::filesystem::current_path();
    buf[n] = 0;
    return std::filesystem::path(buf).parent_path();
#endif
}

std::filesystem::path moduleDir(const void* addrInModule) {
#ifdef _WIN32
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(addrInModule), &mod)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(mod, buf, static_cast<DWORD>(std::size(buf)));
        if (n) return std::filesystem::path(std::wstring(buf, n)).parent_path();
    }
#else
    (void)addrInModule;
#endif
    return exeDir();
}

std::filesystem::path root() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_root.empty()) {
        if (const char* env = std::getenv("DSCOOP_ROOT"); env && *env) g_root = env;
        else g_root = exeDir() / "dscoop";
    }
    return g_root;
}
void setRoot(const std::filesystem::path& p) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_root = p;
}

std::filesystem::path logs() { return root() / "logs"; }
std::filesystem::path cache() { return root() / "cache"; }
std::filesystem::path dumps() { return root() / "dumps"; }
std::filesystem::path backups() { return root() / "backups"; }
std::filesystem::path reports() { return root() / "reports"; }
std::filesystem::path plugins() { return root() / "plugins"; }
std::filesystem::path data() { return root() / "data"; }
std::filesystem::path offsetsJson() { return data() / "offsets.json"; }
std::filesystem::path loaderConfig() { return root() / "dsloader.json"; }
std::filesystem::path pluginConfig() { return root() / "dscoop.json"; }

void ensureTree() {
    std::error_code ec;
    for (const auto& p : {root(), logs(), cache(), dumps(), backups(), reports(), plugins(), data()})
        std::filesystem::create_directories(p, ec);
}

std::string timestampForFiles() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

}  // namespace dscommon::paths
