// DSCoopSetup.exe entry point.
//   GUI (default): ui.cpp — animated installer.
//   Silent mode for scripts/CI:
//     DSCoopSetup.exe /S [/dir=PATH] [/nick=NAME] [/relay=HOST[:PORT]] [/nobackup] [/log=FILE]
//     DSCoopSetup.exe /S /uninstall [/dir=PATH] [/purge]
//   Exit codes: 0 ok, 1 error, 2 game not found, 3 game is running.
//   Demo recording: DSCoopSetup.exe /preview=DIR (scripted session -> DIR\frame_NNNN.png, installs nothing).
#include <fstream>

#include "sys.hpp"

#include <objbase.h>

#include "dscommon/version.hpp"
#include "u8.hpp"

using namespace dsinst;
using namespace dsinst::win;

namespace dsinst::ui {
int run(HINSTANCE inst, int show, const win::Args& args);
}

namespace {
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
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const Args args = parseArgs();
    const int rc = args.silent ? runSilent(args) : ui::run(inst, show, args);
    CoUninitialize();
    return rc;
}
