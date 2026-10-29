#pragma once
// Platform-neutral install / update / uninstall logic. The Windows front-end (GUI + silent mode)
// adds only what needs Win32: Steam registry lookup, resources, elevation, the Apps & Features entry.
//
// Layout produced in the game folder:
//   version.dll                         DSLoader
//   dscoop/plugins/DSCoop.dll           plugin
//   dscoop/data/offsets.json            signatures
//   dscoop/dscoop.json                  user settings (created once, then only nick/relay are touched)
//   dscoop/install.json                 manifest of what we installed (used by uninstall/update)
//   dscoop/chain/version_original.dll   a foreign version.dll we displaced (DSLoader chain-loads it)
//   dscoop/backups/saves_install_<ts>/  copy of the saves made before installing
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dsinst {

namespace fs = std::filesystem;
using Logger = std::function<void(const std::string& line)>;

struct PayloadFile {
    std::string relPath;          // relative to the game folder, '/' separators
    std::vector<uint8_t> data;
};

struct InstallOptions {
    fs::path gameDir;
    std::vector<PayloadFile> payload;
    std::string version;          // DSCoop version being installed
    std::string nick;             // empty = keep existing
    std::string defaultNick;      // used only when the settings have no nick yet (e.g. Steam name)
    std::string relayHost;        // empty = keep existing
    uint16_t relayPort = 27000;
    bool backupSaves = true;
    std::vector<fs::path> saveDirs;   // folders to copy before installing
    fs::path installerExe;            // copied to dscoop/DSCoopSetup.exe for later uninstall (optional)
};

struct InstallState {
    bool installed = false;           // manifest present
    std::string version;              // from manifest
    bool loaderPresent = false;       // version.dll exists
    bool loaderIsOurs = false;        // version.dll exports DSLoader_GetVersion
    bool foreignLoader = false;       // version.dll exists and is NOT ours (another mod)
    bool chainedForeign = false;      // a displaced foreign version.dll sits in dscoop/chain/
};

struct Result {
    bool ok = false;
    std::string error;                // short, user-facing
};

InstallState inspect(const fs::path& gameDir);

/// Can we create files in `dir`? (false = needs elevation or the folder is read-only)
bool canWrite(const fs::path& dir, std::string* err = nullptr);

Result install(const InstallOptions& opts, const Logger& log);

/// Removes mod files, restores a displaced foreign version.dll. Keeps logs, settings and backups
/// unless `removeUserData` is set (save backups are never deleted).
Result uninstall(const fs::path& gameDir, bool removeUserData, const Logger& log);

/// yyyyMMdd-HHmmss
std::string timestamp();

/// Compare dotted versions ("0.10.0" > "0.9.3"). Returns <0, 0, >0.
int compareVersions(const std::string& a, const std::string& b);

}  // namespace dsinst
