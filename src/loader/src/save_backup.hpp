#pragma once
// Copies Death Stranding save folders to dscoop/backups/saves_<timestamp>/ before anything else
// touches the game. Nothing is ever deleted except backups beyond `keep` (oldest first).
#include <filesystem>
#include <string>
#include <vector>

namespace dsloader {

struct BackupResult {
    bool ok = false;
    std::vector<std::string> sources;  // directories that were backed up
    std::string destination;
    uintmax_t bytes = 0;
    std::string error;
};

/// Candidate save directories: %LOCALAPPDATA%\KojimaProductions\DeathStrandingDC and
/// ...\DeathStranding (Steam/Epic both use LOCALAPPDATA). `extra` from dsloader.json is appended.
std::vector<std::filesystem::path> defaultSaveDirs(const std::vector<std::string>& extra);

BackupResult backupSaves(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& backupsDir, size_t keep);

}  // namespace dsloader
