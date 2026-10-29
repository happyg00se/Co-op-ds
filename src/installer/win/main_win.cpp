// DSCoopSetup.exe — one-file installer for players.
//
// GUI: finds Death Stranding in Steam on its own, takes the nick from Steam, backs up the saves,
// installs/updates/removes the mod, registers itself in "Apps & features", launches the game.
// Silent mode for scripts/CI:
//   DSCoopSetup.exe /S [/dir=PATH] [/nick=NAME] [/relay=HOST[:PORT]] [/nobackup] [/log=FILE]
//   DSCoopSetup.exe /S /uninstall [/dir=PATH] [/purge]
// Exit codes: 0 ok, 1 error, 2 game not found, 3 game is running.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "dscommon/version.hpp"
#include "install.hpp"
#include "pe.hpp"
#include "resource_ids.h"
#include "steam.hpp"
#include "u8.hpp"

namespace fs = std::filesystem;
using namespace dsinst;

namespace {

// ---------------------------------------------------------------------------- text
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

struct Strings {
    const wchar_t* title;
    const wchar_t* subtitle;
    const wchar_t* gameFolder;
    const wchar_t* browse;
    const wchar_t* nick;
    const wchar_t* relay;
    const wchar_t* relayHint;
    const wchar_t* backup;
    const wchar_t* install;
    const wchar_t* update;
    const wchar_t* reinstall;
    const wchar_t* uninstall;
    const wchar_t* play;
    const wchar_t* close;
    const wchar_t* searching;
    const wchar_t* foundDC;
    const wchar_t* foundOriginal;
    const wchar_t* notFound;
    const wchar_t* manualFolder;
    const wchar_t* stateNone;
    const wchar_t* stateInstalled;
    const wchar_t* foreignMod;
    const wchar_t* gameRunning;
    const wchar_t* needAdmin;
    const wchar_t* doneInstall;
    const wchar_t* doneUninstall;
    const wchar_t* failed;
    const wchar_t* confirmUninstall;
    const wchar_t* pickFolder;
    const wchar_t* noExe;
    const wchar_t* olderThanInstalled;
};

const Strings kRu = {
    L"DSCoop — кооператив для Death Stranding",
    L"Установщик всё сделает сам: найдёт игру, сохранит копию сейвов и положит мод рядом с игрой. Файлы игры не изменяются.",
    L"Папка игры:",
    L"Выбрать…",
    L"Ник в кооперативе:",
    L"Сервер-релей:",
    L"Необязательно. Нужен для входа по коду приглашения. Без него — подключение по IP.",
    L"Сделать копию сохранений перед установкой (рекомендуется)",
    L"Установить",
    L"Обновить",
    L"Переустановить",
    L"Удалить",
    L"Играть",
    L"Закрыть",
    L"Ищу игру в Steam…",
    L"Найдена: Death Stranding Director's Cut (Steam).",
    L"Найдена обычная Death Stranding, а мод сделан для Director's Cut. Установить можно, но работать он может неправильно.",
    L"Игра в Steam не найдена. Нажми «Выбрать…» и укажи папку игры.",
    L"Папка выбрана вручную.",
    L"Мод ещё не установлен.",
    L"Установлен DSCoop ",
    L"В папке игры уже есть version.dll другого мода. Он не пропадёт: установщик отложит его, и DSCoop загрузит его вместе с собой.",
    L"Игра сейчас запущена. Закрой Death Stranding и нажми кнопку ещё раз.",
    L"Для записи в папку игры нужны права администратора. Сейчас Windows спросит разрешение.",
    L"Готово! Запусти игру (кнопка «Играть» или через Steam) и в главном меню нажми F9 — откроется меню «Совместная игра».",
    L"DSCoop удалён. Игра снова обычная. Папка dscoop с копиями сохранений и логами оставлена.",
    L"Не получилось: ",
    L"Удалить DSCoop из игры?\n\nКопии сохранений и настройки останутся в папке dscoop.",
    L"Выбери папку Death Stranding (там, где лежит ds.exe)",
    L"В этой папке не видно exe-файла игры. Точно та папка?",
    L"Установлена более новая версия DSCoop. Этот установщик её заменит на ",
};

const Strings kEn = {
    L"DSCoop — co-op for Death Stranding",
    L"The installer does everything for you: finds the game, backs up your saves and puts the mod next to the game. Game files are not modified.",
    L"Game folder:",
    L"Browse…",
    L"Co-op nickname:",
    L"Relay server:",
    L"Optional. Needed to join by invite code. Without it you connect by IP.",
    L"Back up my saves before installing (recommended)",
    L"Install",
    L"Update",
    L"Reinstall",
    L"Uninstall",
    L"Play",
    L"Close",
    L"Looking for the game in Steam…",
    L"Found: Death Stranding Director's Cut (Steam).",
    L"Found the original Death Stranding, but the mod targets the Director's Cut. You can install it, but it may not work correctly.",
    L"The game was not found in Steam. Click “Browse…” and pick the game folder.",
    L"Folder chosen manually.",
    L"The mod is not installed yet.",
    L"Installed: DSCoop ",
    L"Another mod's version.dll is already in the game folder. It will not be lost: the installer moves it aside and DSCoop loads it together with itself.",
    L"The game is running. Close Death Stranding and click the button again.",
    L"Writing to the game folder needs administrator rights. Windows will ask for permission now.",
    L"Done! Start the game (the “Play” button or Steam) and press F9 in the main menu to open the co-op menu.",
    L"DSCoop removed. The game is vanilla again. The dscoop folder with save backups and logs was kept.",
    L"Failed: ",
    L"Remove DSCoop from the game?\n\nSave backups and settings stay in the dscoop folder.",
    L"Pick the Death Stranding folder (the one with ds.exe)",
    L"No game executable in this folder. Are you sure it is the right one?",
    L"A newer DSCoop is installed. This installer will replace it with ",
};

const Strings* g_s = &kEn;
void pickLanguage() {
    const LANGID lang = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(lang)) {
        case LANG_RUSSIAN: case LANG_UKRAINIAN: case LANG_BELARUSIAN: case LANG_KAZAK: g_s = &kRu; break;
        default: g_s = &kEn; break;
    }
}

// ---------------------------------------------------------------------------- args
struct Args {
    bool silent = false;
    bool uninstall = false;
    bool purge = false;
    bool noBackup = false;
    bool autostart = false;  // elevated relaunch: press the main button immediately
    std::wstring dir, nick, relay, logFile;
};

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
    }
    if (argv) LocalFree(argv);
    return a;
}

// ---------------------------------------------------------------------------- system
fs::path selfPath() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n));
}

std::optional<std::wstring> regString(HKEY root, const wchar_t* sub, const wchar_t* name, DWORD extraFlags = 0) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, sub, name, RRF_RT_REG_SZ | extraFlags, nullptr, buf, &size) != ERROR_SUCCESS) return std::nullopt;
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

struct Detected {
    std::optional<steam::GameInstall> game;
    std::optional<std::string> persona;
};

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

std::vector<uint8_t> resourceBytes(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!r) return {};
    HGLOBAL h = LoadResource(nullptr, r);
    const DWORD size = SizeofResource(nullptr, r);
    const auto* p = static_cast<const uint8_t*>(LockResource(h));
    if (!p || !size) return {};
    return std::vector<uint8_t>(p, p + size);
}

std::vector<PayloadFile> payload() {
    return {
        {"version.dll", resourceBytes(IDR_PAYLOAD_LOADER)},
        {"dscoop/plugins/DSCoop.dll", resourceBytes(IDR_PAYLOAD_PLUGIN)},
        {"dscoop/data/offsets.json", resourceBytes(IDR_PAYLOAD_OFFSETS)},
    };
}

constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DSCoop";

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
    setStr(L"DisplayIcon", setup.wstring());
    setStr(L"UninstallString", L"\"" + setup.wstring() + L"\" /uninstall");
    setStr(L"QuietUninstallString", L"\"" + setup.wstring() + L"\" /uninstall /S");
    setDw(L"NoModify", 1);
    setDw(L"NoRepair", 1);
    setDw(L"EstimatedSize", 8 * 1024);
    RegCloseKey(k);
}

void unregisterUninstall() { RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey); }

/// When uninstalling from <game>\dscoop\DSCoopSetup.exe, delete that copy after we exit.
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

/// Launched as <game>\dscoop\DSCoopSetup.exe (from Apps & features)?
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

// ---------------------------------------------------------------------------- silent mode
int runSilent(const Args& a) {
    fs::path logPath = a.logFile.empty() ? fs::temp_directory_path() / L"DSCoopSetup.log" : fs::path(a.logFile);
    std::ofstream logf(logPath, std::ios::app);
    auto log = [&](const std::string& s) { logf << s << "\n"; logf.flush(); };
    log("DSCoopSetup " DSCOOP_VERSION_STRING " silent " + std::string(a.uninstall ? "uninstall" : "install"));

    fs::path gameDir = a.dir;
    std::optional<std::string> persona;
    if (gameDir.empty()) {
        if (auto g = gameDirFromSelf()) gameDir = *g;
    }
    if (gameDir.empty() || !a.uninstall) {
        Detected d = detectSteam();
        persona = d.persona;
        if (gameDir.empty() && d.game) gameDir = d.game->gameDir;
    }
    if (gameDir.empty()) { log("error: game not found, pass /dir=PATH"); return 2; }
    log("game folder: " + utf8(gameDir));
    if (processRunning(steam::findGameExe(gameDir))) { log("error: the game is running"); return 3; }

    Result r;
    if (a.uninstall) {
        r = uninstall(gameDir, a.purge, log);
        if (r.ok) { unregisterUninstall(); scheduleSelfDelete(); }
    } else {
        InstallOptions o;
        o.gameDir = gameDir;
        o.payload = payload();
        o.version = DSCOOP_VERSION_STRING;
        o.nick = U(a.nick);                      // explicit /nick= wins
        o.defaultNick = persona.value_or("");    // Steam name only if the player has no nick yet
        if (!a.relay.empty()) splitRelay(a.relay, o.relayHost, o.relayPort);
        o.backupSaves = !a.noBackup;
        o.saveDirs = saveDirs();
        o.installerExe = selfPath();
        r = install(o, log);
        if (r.ok) registerUninstall(gameDir);
    }
    log(r.ok ? "ok" : "error: " + r.error);
    return r.ok ? 0 : 1;
}

// ---------------------------------------------------------------------------- GUI
enum : int {
    IDC_TITLE = 100, IDC_SUBTITLE, IDC_DIR_LABEL, IDC_DIR, IDC_BROWSE, IDC_STATUS,
    IDC_NICK_LABEL, IDC_NICK, IDC_RELAY_LABEL, IDC_RELAY, IDC_RELAY_HINT, IDC_BACKUP,
    IDC_LOG, IDC_INSTALL, IDC_UNINSTALL, IDC_PLAY, IDC_CLOSE,
};
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;

struct App {
    HWND wnd = nullptr;
    HFONT font = nullptr, fontTitle = nullptr;
    float scale = 1.0f;
    Args args;
    fs::path gameDir;
    std::optional<steam::GameInstall> game;
    bool manualDir = false;
    std::atomic<bool> busy{false};
    bool lastWasUninstall = false;
};
App g_app;

int S(int v) { return static_cast<int>(static_cast<float>(v) * g_app.scale + 0.5f); }
HWND item(int id) { return GetDlgItem(g_app.wnd, id); }
std::wstring textOf(int id) {
    const int n = GetWindowTextLengthW(item(id));
    std::wstring s(static_cast<size_t>(n), L'\0');
    GetWindowTextW(item(id), s.data(), n + 1);
    return s;
}

void appendLog(const std::wstring& line) {
    HWND log = item(IDC_LOG);
    const int len = GetWindowTextLengthW(log);
    SendMessageW(log, EM_SETSEL, len, len);
    const std::wstring withNl = (len ? L"\r\n" : L"") + line;
    SendMessageW(log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(withNl.c_str()));
}

void postLog(const std::string& line) {
    PostMessageW(g_app.wnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(new std::wstring(W(line))));
}

void refreshState() {
    const bool haveDir = !g_app.gameDir.empty();
    SetWindowTextW(item(IDC_DIR), haveDir ? g_app.gameDir.wstring().c_str() : L"");
    std::wstring status;
    if (g_app.game && !g_app.manualDir) status = g_app.game->directorsCut() ? g_s->foundDC : g_s->foundOriginal;
    else if (haveDir) status = g_s->manualFolder;
    else status = g_s->notFound;

    InstallState st;
    if (haveDir) st = inspect(g_app.gameDir);
    status += L"  ";
    status += st.installed ? std::wstring(g_s->stateInstalled) + W(st.version) + L"." : g_s->stateNone;
    if (st.foreignLoader) status += std::wstring(L"\n") + g_s->foreignMod;
    SetWindowTextW(item(IDC_STATUS), status.c_str());

    const wchar_t* mainLabel = g_s->install;
    if (st.installed) {
        const int cmp = compareVersions(st.version, DSCOOP_VERSION_STRING);
        mainLabel = cmp < 0 ? g_s->update : g_s->reinstall;
    }
    SetWindowTextW(item(IDC_INSTALL), mainLabel);
    const bool idle = !g_app.busy;
    EnableWindow(item(IDC_INSTALL), idle && haveDir);
    EnableWindow(item(IDC_UNINSTALL), idle && haveDir && (st.installed || st.loaderIsOurs));
    EnableWindow(item(IDC_PLAY), idle && st.installed && g_app.game.has_value());
    EnableWindow(item(IDC_BROWSE), idle);
}

bool relaunchElevated(const std::wstring& extra) {
    std::wstring params = L"/dir=\"" + g_app.gameDir.wstring() + L"\" /autostart" + extra;
    const std::wstring nick = textOf(IDC_NICK), relay = textOf(IDC_RELAY);
    if (!nick.empty()) params += L" \"/nick=" + nick + L"\"";
    if (!relay.empty()) params += L" \"/relay=" + relay + L"\"";
    if (BST_CHECKED != SendMessageW(item(IDC_BACKUP), BM_GETCHECK, 0, 0)) params += L" /nobackup";
    const std::wstring exe = selfPath().wstring();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

/// Shared checks before install/uninstall. Returns false if the action must not start.
bool preflight(bool forUninstall) {
    if (processRunning(steam::findGameExe(g_app.gameDir))) {
        MessageBoxW(g_app.wnd, g_s->gameRunning, L"DSCoop", MB_ICONWARNING);
        return false;
    }
    if (!canWrite(g_app.gameDir)) {
        if (isElevated()) {
            MessageBoxW(g_app.wnd, (std::wstring(g_s->failed) + W("no write access to " + utf8(g_app.gameDir))).c_str(), L"DSCoop", MB_ICONERROR);
            return false;
        }
        MessageBoxW(g_app.wnd, g_s->needAdmin, L"DSCoop", MB_ICONINFORMATION);
        if (relaunchElevated(forUninstall ? L" /uninstall" : L"")) DestroyWindow(g_app.wnd);
        return false;
    }
    return true;
}

void startInstall() {
    if (g_app.busy || g_app.gameDir.empty() || !preflight(false)) return;
    const InstallState st = inspect(g_app.gameDir);
    if (st.installed && compareVersions(st.version, DSCOOP_VERSION_STRING) > 0) {
        const std::wstring q = std::wstring(g_s->olderThanInstalled) + W(DSCOOP_VERSION_STRING) + L".";
        if (MessageBoxW(g_app.wnd, q.c_str(), L"DSCoop", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
    }
    InstallOptions o;
    o.gameDir = g_app.gameDir;
    o.payload = payload();
    o.version = DSCOOP_VERSION_STRING;
    o.nick = U(textOf(IDC_NICK));
    splitRelay(textOf(IDC_RELAY), o.relayHost, o.relayPort);
    o.backupSaves = BST_CHECKED == SendMessageW(item(IDC_BACKUP), BM_GETCHECK, 0, 0);
    o.saveDirs = saveDirs();
    o.installerExe = selfPath();
    g_app.busy = true;
    g_app.lastWasUninstall = false;
    refreshState();
    std::thread([o] {
        Result r = install(o, postLog);
        if (r.ok) registerUninstall(o.gameDir);
        else postLog("error: " + r.error);
        PostMessageW(g_app.wnd, WM_APP_DONE, r.ok ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(W(r.error))));
    }).detach();
}

void startUninstall(bool ask) {
    if (g_app.busy || g_app.gameDir.empty()) return;
    if (ask && MessageBoxW(g_app.wnd, g_s->confirmUninstall, L"DSCoop", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!preflight(true)) return;
    g_app.busy = true;
    g_app.lastWasUninstall = true;
    refreshState();
    const fs::path dir = g_app.gameDir;
    std::thread([dir] {
        Result r = uninstall(dir, false, postLog);
        if (r.ok) unregisterUninstall();
        else postLog("error: " + r.error);
        PostMessageW(g_app.wnd, WM_APP_DONE, r.ok ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(W(r.error))));
    }).detach();
}

void browseFolder() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(g_s->pickFolder);
    if (SUCCEEDED(dlg->Show(g_app.wnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                fs::path p(path);
                CoTaskMemFree(path);
                bool accept = true;
                if (steam::findGameExe(p).empty())
                    accept = MessageBoxW(g_app.wnd, g_s->noExe, L"DSCoop", MB_YESNO | MB_ICONQUESTION) == IDYES;
                if (accept) { g_app.gameDir = p; g_app.manualDir = true; }
            }
            it->Release();
        }
    }
    dlg->Release();
    refreshState();
}

HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_app.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
    return c;
}

void createControls() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_app.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW big = ncm.lfMessageFont;
    big.lfHeight = static_cast<LONG>(big.lfHeight * 1.6);
    big.lfWeight = FW_BOLD;
    g_app.fontTitle = CreateFontIndirectW(&big);

    HWND title = mk(L"STATIC", g_s->title, 0, 16, 12, 600, 30, IDC_TITLE);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.fontTitle), TRUE);
    mk(L"STATIC", g_s->subtitle, 0, 16, 46, 600, 34, IDC_SUBTITLE);

    mk(L"STATIC", g_s->gameFolder, 0, 16, 92, 200, 18, IDC_DIR_LABEL);
    mk(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, 16, 112, 488, 24, IDC_DIR);
    mk(L"BUTTON", g_s->browse, BS_PUSHBUTTON | WS_TABSTOP, 512, 111, 104, 26, IDC_BROWSE);
    mk(L"STATIC", g_s->searching, 0, 16, 142, 600, 50, IDC_STATUS);

    mk(L"STATIC", g_s->nick, 0, 16, 202, 150, 18, IDC_NICK_LABEL);
    HWND nick = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 170, 199, 220, 24, IDC_NICK);
    SendMessageW(nick, EM_SETLIMITTEXT, 24, 0);
    mk(L"STATIC", g_s->relay, 0, 16, 234, 150, 18, IDC_RELAY_LABEL);
    HWND relay = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 170, 231, 320, 24, IDC_RELAY);
    SendMessageW(relay, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"relay.example.com:27000"));
    mk(L"STATIC", g_s->relayHint, 0, 170, 258, 446, 18, IDC_RELAY_HINT);
    HWND backup = mk(L"BUTTON", g_s->backup, BS_AUTOCHECKBOX | WS_TABSTOP, 16, 284, 600, 22, IDC_BACKUP);
    SendMessageW(backup, BM_SETCHECK, BST_CHECKED, 0);

    mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 16, 314, 600, 120, IDC_LOG);

    mk(L"BUTTON", g_s->install, BS_DEFPUSHBUTTON | WS_TABSTOP, 16, 446, 160, 32, IDC_INSTALL);
    mk(L"BUTTON", g_s->uninstall, BS_PUSHBUTTON | WS_TABSTOP, 184, 446, 120, 32, IDC_UNINSTALL);
    mk(L"BUTTON", g_s->play, BS_PUSHBUTTON | WS_TABSTOP, 312, 446, 120, 32, IDC_PLAY);
    mk(L"BUTTON", g_s->close, BS_PUSHBUTTON | WS_TABSTOP, 496, 446, 120, 32, IDC_CLOSE);
}

void detectInitial() {
    Detected d;
    if (!g_app.args.dir.empty()) {
        g_app.gameDir = g_app.args.dir;
        g_app.manualDir = true;
        d = detectSteam();
        if (d.game) { std::error_code ec; if (fs::equivalent(d.game->gameDir, g_app.gameDir, ec)) { g_app.game = d.game; g_app.manualDir = false; } }
    } else if (auto self = gameDirFromSelf()) {
        g_app.gameDir = *self;
        d = detectSteam();
        if (d.game) g_app.game = d.game;
    } else {
        d = detectSteam();
        if (d.game) { g_app.game = d.game; g_app.gameDir = d.game->gameDir; }
    }
    // Nick: explicit arg > existing dscoop.json > Steam name.
    std::string nick = U(g_app.args.nick);
    std::string relay = U(g_app.args.relay);
    if (!g_app.gameDir.empty()) {
        const std::string cfgText = steam::readText(g_app.gameDir / "dscoop" / "dscoop.json");
        if (!cfgText.empty()) {
            try {
                auto j = nlohmann::json::parse(cfgText, nullptr, true, true);
                if (nick.empty()) nick = j.value("nick", "");
                if (relay.empty() && j.contains("relay") && j["relay"].is_object()) {
                    const std::string h = j["relay"].value("host", "");
                    const int p = j["relay"].value("control_port", 27000);
                    if (!h.empty()) relay = p == 27000 ? h : h + ":" + std::to_string(p);
                }
            } catch (...) {}
        }
    }
    if (nick.empty() || nick == "Porter") nick = d.persona.value_or("Porter");
    SetWindowTextW(item(IDC_NICK), W(nick).c_str());
    SetWindowTextW(item(IDC_RELAY), W(relay).c_str());
    if (g_app.args.noBackup) SendMessageW(item(IDC_BACKUP), BM_SETCHECK, BST_UNCHECKED, 0);
    refreshState();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_app.wnd = hwnd;
            createControls();
            PostMessageW(hwnd, WM_APP + 10, 0, 0);
            return 0;
        case WM_APP + 10:
            detectInitial();
            if (g_app.args.autostart) {
                if (g_app.args.uninstall) startUninstall(false);
                else startInstall();
            } else if (g_app.args.uninstall) {
                startUninstall(true);
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            // Read-only edit boxes and labels on the window background.
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            if (reinterpret_cast<HWND>(lp) == item(IDC_STATUS)) SetTextColor(dc, RGB(20, 90, 160));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE: browseFolder(); break;
                case IDC_INSTALL: startInstall(); break;
                case IDC_UNINSTALL: startUninstall(true); break;
                case IDC_PLAY: {
                    const std::wstring url = L"steam://rungameid/" + W(g_app.game ? g_app.game->appId : steam::kAppDirectorsCut);
                    ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
                case IDC_CLOSE: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_APP_LOG: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
            appendLog(*s);
            return 0;
        }
        case WM_APP_DONE: {
            std::unique_ptr<std::wstring> err(reinterpret_cast<std::wstring*>(lp));
            g_app.busy = false;
            refreshState();
            if (wp) {
                MessageBoxW(hwnd, g_app.lastWasUninstall ? g_s->doneUninstall : g_s->doneInstall, L"DSCoop", MB_ICONINFORMATION);
                if (g_app.lastWasUninstall && gameDirFromSelf()) { scheduleSelfDelete(); DestroyWindow(hwnd); }
            } else {
                MessageBoxW(hwnd, (std::wstring(g_s->failed) + *err).c_str(), L"DSCoop", MB_ICONERROR);
            }
            return 0;
        }
        case WM_CLOSE:
            if (g_app.busy) return 0;  // never leave a half-installed game
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int runGui(HINSTANCE inst, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(nullptr);
    g_app.scale = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSY)) / 96.0f;
    ReleaseDC(nullptr, screen);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"DSCoopSetup";
    RegisterClassExW(&wc);

    RECT rc{0, 0, S(632), S(494)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const std::wstring caption = L"DSCoop " + W(DSCOOP_VERSION_STRING) + L" — Setup";
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, caption.c_str(), style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                                (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    pickLanguage();
    g_app.args = parseArgs();
    const int rc = g_app.args.silent ? runSilent(g_app.args) : runGui(inst, show);
    CoUninitialize();
    return rc;
}
