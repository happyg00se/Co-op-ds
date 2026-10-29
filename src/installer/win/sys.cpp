#include "sys.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <nlohmann/json.hpp>

#include "dscommon/version.hpp"
#include "resource_ids.h"

namespace dsinst::win {

std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string U(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

Args parseArgs() {
    Args a;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring s = argv[i];
        auto val = [&](const wchar_t* key) -> std::optional<std::wstring> {
            const size_t n = wcslen(key);
            if (_wcsnicmp(s.c_str(), key, n) == 0) return s.substr(n);
            return std::nullopt;
        };
        if (_wcsicmp(s.c_str(), L"/S") == 0 || _wcsicmp(s.c_str(), L"/silent") == 0) a.silent = true;
        else if (_wcsicmp(s.c_str(), L"/uninstall") == 0) a.uninstall = true;
        else if (_wcsicmp(s.c_str(), L"/purge") == 0) a.purge = true;
        else if (_wcsicmp(s.c_str(), L"/nobackup") == 0) a.noBackup = true;
        else if (_wcsicmp(s.c_str(), L"/autostart") == 0) a.autostart = true;
        else if (auto d = val(L"/dir=")) a.dir = *d;
        else if (auto n = val(L"/nick=")) a.nick = *n;
        else if (auto r = val(L"/relay=")) a.relay = *r;
        else if (auto l = val(L"/log=")) a.logFile = *l;
        else if (auto pv = val(L"/preview=")) a.preview = *pv;
    }
    if (argv) LocalFree(argv);
    return a;
}

fs::path selfPath() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n));
}

namespace {
std::optional<std::wstring> regString(HKEY root, const wchar_t* sub, const wchar_t* name) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, sub, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return std::nullopt;
    return std::wstring(buf);
}

std::vector<fs::path> steamRoots() {
    std::vector<fs::path> roots;
    if (auto p = regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) roots.emplace_back(*p);
    if (auto p = regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath")) roots.emplace_back(*p);
    if (auto p = regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath")) roots.emplace_back(*p);
    roots.emplace_back(L"C:\\Program Files (x86)\\Steam");
    roots.emplace_back(L"C:\\Program Files\\Steam");
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto& r : roots) {
        r = r.lexically_normal();
        if (!fs::is_directory(r, ec)) continue;
        bool dup = false;
        for (auto& o : out) if (fs::equivalent(o, r, ec)) dup = true;
        if (!dup) out.push_back(r);
    }
    return out;
}
}  // namespace

Detected detectSteam() {
    Detected d;
    for (const auto& root : steamRoots()) {
        if (!d.persona) d.persona = steam::mostRecentPersona(root);
        if (!d.game) d.game = steam::findDeathStranding(root);
        if (d.game && d.persona) break;
    }
    return d;
}

std::vector<fs::path> saveDirs() {
    std::vector<fs::path> out;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        out.push_back(fs::path(local) / L"KojimaProductions");
        CoTaskMemFree(local);
    }
    return out;
}

bool processRunning(const fs::path& exe) {
    if (exe.empty()) return false;
    const std::wstring name = exe.filename().wstring();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, name.c_str()) == 0) { found = true; break; }
    }
    CloseHandle(snap);
    return found;
}

bool isElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    const bool r = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n) && e.TokenIsElevated;
    CloseHandle(tok);
    return r;
}

bool relaunchElevated(const std::wstring& params) {
    const std::wstring exe = selfPath().wstring();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

std::span<const uint8_t> resource(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!r) return {};
    HGLOBAL h = LoadResource(nullptr, r);
    const DWORD size = SizeofResource(nullptr, r);
    const auto* p = static_cast<const uint8_t*>(LockResource(h));
    if (!p || !size) return {};
    return {p, size};
}

std::vector<PayloadFile> payload() {
    auto vec = [](int id) { auto s = resource(id); return std::vector<uint8_t>(s.begin(), s.end()); };
    return {
        {"version.dll", vec(IDR_PAYLOAD_LOADER)},
        {"dscoop/plugins/DSCoop.dll", vec(IDR_PAYLOAD_PLUGIN)},
        {"dscoop/data/offsets.json", vec(IDR_PAYLOAD_OFFSETS)},
    };
}

namespace {
constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DSCoop";
}

void registerUninstall(const fs::path& gameDir) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    auto setStr = [&](const wchar_t* n, const std::wstring& v) {
        RegSetValueExW(k, n, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    };
    auto setDw = [&](const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)); };
    const fs::path setup = gameDir / L"dscoop" / L"DSCoopSetup.exe";
    setStr(L"DisplayName", L"DSCoop — Death Stranding co-op");
    setStr(L"DisplayVersion", W(DSCOOP_VERSION_STRING));
    setStr(L"Publisher", L"DSCoop");
    setStr(L"InstallLocation", gameDir.wstring());
    setStr(L"DisplayIcon", setup.wstring() + L",0");
    setStr(L"UninstallString", L"\"" + setup.wstring() + L"\" /uninstall");
    setStr(L"QuietUninstallString", L"\"" + setup.wstring() + L"\" /uninstall /S");
    setDw(L"NoModify", 1);
    setDw(L"NoRepair", 1);
    setDw(L"EstimatedSize", 12 * 1024);
    RegCloseKey(k);
}

void unregisterUninstall() { RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey); }

void scheduleSelfDelete() {
    const fs::path me = selfPath();
    if (me.parent_path().filename() != L"dscoop") return;
    std::wstring cmd = L"cmd.exe /C ping -n 3 127.0.0.1 >NUL & del /F /Q \"" + me.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

std::optional<fs::path> gameDirFromSelf() {
    const fs::path me = selfPath();
    std::error_code ec;
    if (me.parent_path().filename() == L"dscoop" && fs::exists(me.parent_path() / L"install.json", ec)) return me.parent_path().parent_path();
    return std::nullopt;
}

void splitRelay(const std::wstring& relay, std::string& host, uint16_t& port) {
    host = U(relay);
    port = 27000;
    const auto c = host.rfind(':');
    if (c != std::string::npos) {
        const long p = std::strtol(host.c_str() + c + 1, nullptr, 10);
        if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
        host.resize(c);
    }
    while (!host.empty() && host.back() == ' ') host.pop_back();
    while (!host.empty() && host.front() == ' ') host.erase(host.begin());
}

std::optional<fs::path> pickFolder(HWND owner, const wchar_t* title) {
    std::optional<fs::path> out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(title);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                out = fs::path(path);
                CoTaskMemFree(path);
            }
            it->Release();
        }
    }
    dlg->Release();
    return out;
}

void launchGame(const std::string& appId) {
    const std::wstring url = L"steam://rungameid/" + W(appId.empty() ? steam::kAppDirectorsCut : appId);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void readSettings(const fs::path& gameDir, std::string& nick, std::string& relay) {
    const std::string text = steam::readText(gameDir / "dscoop" / "dscoop.json");
    if (text.empty()) return;
    try {
        auto j = nlohmann::json::parse(text, nullptr, true, true);
        if (nick.empty()) nick = j.value("nick", "");
        if (relay.empty() && j.contains("relay") && j["relay"].is_object()) {
            const std::string h = j["relay"].value("host", "");
            const int p = j["relay"].value("control_port", 27000);
            if (!h.empty()) relay = p == 27000 ? h : h + ":" + std::to_string(p);
        }
    } catch (...) {
    }
}

}  // namespace dsinst::win
