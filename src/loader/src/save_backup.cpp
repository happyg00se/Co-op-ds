#include "save_backup.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

#include "dscommon/log.hpp"
#include "dscommon/paths.hpp"

namespace dsloader {

std::vector<std::filesystem::path> defaultSaveDirs(const std::vector<std::string>& extra) {
    std::vector<std::filesystem::path> out;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        const std::filesystem::path base(local);
        CoTaskMemFree(local);
        out.push_back(base / L"KojimaProductions" / L"DeathStrandingDC");
        out.push_back(base / L"KojimaProductions" / L"DeathStranding");
    }
    for (const auto& e : extra) if (!e.empty()) out.emplace_back(e);
    return out;
}

BackupResult backupSaves(const std::vector<std::filesystem::path>& sources, const std::filesystem::path& backupsDir, size_t keep) {
    BackupResult r;
    std::error_code ec;
    const auto dest = backupsDir / ("saves_" + dscommon::paths::timestampForFiles());
    for (const auto& src : sources) {
        if (!std::filesystem::is_directory(src, ec)) continue;
        const auto target = dest / src.filename();
        std::filesystem::create_directories(target, ec);
        std::filesystem::copy(src, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            r.error += "copy " + src.string() + ": " + ec.message() + "; ";
            ec.clear();
            continue;
        }
        for (const auto& f : std::filesystem::recursive_directory_iterator(target, ec))
            if (f.is_regular_file(ec)) r.bytes += f.file_size(ec);
        r.sources.push_back(src.string());
    }
    if (r.sources.empty()) {
        r.error = r.error.empty() ? "no save directories found" : r.error;
        return r;
    }
    r.destination = dest.string();
    r.ok = true;
    DSLOG_INFO("saves: backed up {} folder(s), {} KB -> {}", r.sources.size(), r.bytes / 1024, r.destination);

    // Prune: keep the newest `keep` backups.
    std::vector<std::filesystem::path> olds;
    for (const auto& d : std::filesystem::directory_iterator(backupsDir, ec))
        if (d.is_directory(ec) && d.path().filename().string().rfind("saves_", 0) == 0) olds.push_back(d.path());
    std::sort(olds.begin(), olds.end());
    while (olds.size() > keep && keep > 0) {
        DSLOG_INFO("saves: pruning old backup {}", olds.front().string());
        std::filesystem::remove_all(olds.front(), ec);
        olds.erase(olds.begin());
    }
    return r;
}

}  // namespace dsloader
