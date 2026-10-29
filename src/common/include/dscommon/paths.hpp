#pragma once
// Layout of the dscoop/ folder that lives next to the game executable:
//   <game>/version.dll                 DSLoader
//   <game>/dscoop/dsloader.json        loader config
//   <game>/dscoop/dscoop.json          plugin/user config (nick, relay, ...)
//   <game>/dscoop/plugins/*.dll        plugins (DSCoop.dll)
//   <game>/dscoop/data/offsets.json    signatures + offsets
//   <game>/dscoop/{logs,cache,dumps,backups,reports}/
#include <filesystem>
#include <string>

namespace dscommon::paths {

/// Directory of the running executable (game exe, or the tool).
std::filesystem::path exeDir();
/// Directory of the module that contains `addrInModule` (DLL self-locate). Returns exeDir() on non-Windows.
std::filesystem::path moduleDir(const void* addrInModule);

/// Root of the dscoop/ tree. Override with env DSCOOP_ROOT (useful for tools and tests).
std::filesystem::path root();
void setRoot(const std::filesystem::path& p);

std::filesystem::path logs();
std::filesystem::path cache();
std::filesystem::path dumps();
std::filesystem::path backups();
std::filesystem::path reports();
std::filesystem::path plugins();
std::filesystem::path data();
std::filesystem::path offsetsJson();
std::filesystem::path loaderConfig();
std::filesystem::path pluginConfig();

/// Create every directory above (idempotent, never throws).
void ensureTree();

/// yyyyMMdd-HHmmss for file names.
std::string timestampForFiles();

}  // namespace dscommon::paths
