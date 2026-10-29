#pragma once
// Locate Death Stranding in Steam libraries without touching the registry (the Windows front-end
// passes the Steam root it read from HKCU\Software\Valve\Steam\SteamPath).
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dsinst::steam {

namespace fs = std::filesystem;

inline constexpr const char* kAppDirectorsCut = "1850570";
inline constexpr const char* kAppOriginal = "1190460";

struct GameInstall {
    std::string appId;
    std::string name;     // "DEATH STRANDING DIRECTOR'S CUT"
    fs::path library;     // ...\SteamLibrary
    fs::path gameDir;     // ...\steamapps\common\<installdir>
    fs::path exe;         // detected game executable (may be empty)
    bool directorsCut() const { return appId == kAppDirectorsCut; }
};

/// All library roots (the Steam root itself is always first).
std::vector<fs::path> libraryFolders(const fs::path& steamRoot);

/// Find an installed app by id across all libraries.
std::optional<GameInstall> findApp(const fs::path& steamRoot, const std::string& appId);

/// Director's Cut first, then the original release.
std::optional<GameInstall> findDeathStranding(const fs::path& steamRoot);

/// Game executable inside a folder: known names first, otherwise the largest .exe at top level.
fs::path findGameExe(const fs::path& gameDir);

/// Steam display name of the most recently logged-in user (config/loginusers.vdf).
std::optional<std::string> mostRecentPersona(const fs::path& steamRoot);

/// Read a whole text file (empty on failure).
std::string readText(const fs::path& p);

}  // namespace dsinst::steam
