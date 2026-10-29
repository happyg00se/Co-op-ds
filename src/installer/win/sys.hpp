#pragma once
// Win32 helpers shared by the GUI (ui.cpp) and the silent mode (main_win.cpp).
#include <windows.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "install.hpp"
#include "steam.hpp"

namespace dsinst::win {

namespace fs = std::filesystem;

std::wstring W(const std::string& s);
std::string U(const std::wstring& w);

struct Args {
    bool silent = false;
    bool uninstall = false;
    bool purge = false;
    bool noBackup = false;
    bool autostart = false;  // elevated relaunch: start the action immediately
    std::wstring dir, nick, relay, logFile;
    std::wstring preview;    // /preview=DIR: render a scripted demo to DIR\frame_NNNN.png (docs/marketing), no install
};
Args parseArgs();

fs::path selfPath();

struct Detected {
    std::optional<steam::GameInstall> game;
    std::optional<std::string> persona;
};
Detected detectSteam();

std::vector<fs::path> saveDirs();
bool processRunning(const fs::path& exe);
bool isElevated();
bool relaunchElevated(const std::wstring& params);

/// RCDATA resource of this exe (memory stays valid for the process lifetime).
std::span<const uint8_t> resource(int id);
std::vector<PayloadFile> payload();

void registerUninstall(const fs::path& gameDir);
void unregisterUninstall();
/// When running as <game>\dscoop\DSCoopSetup.exe, delete that copy after we exit.
void scheduleSelfDelete();
/// Launched as <game>\dscoop\DSCoopSetup.exe (from Apps & features)?
std::optional<fs::path> gameDirFromSelf();
void splitRelay(const std::wstring& relay, std::string& host, uint16_t& port);
std::optional<fs::path> pickFolder(HWND owner, const wchar_t* title);
void launchGame(const std::string& appId);
/// Reads nick and relay from an existing dscoop.json.
void readSettings(const fs::path& gameDir, std::string& nick, std::string& relay);

}  // namespace dsinst::win
