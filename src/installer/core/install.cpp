#include "install.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>

#include <nlohmann/json.hpp>

#include "pe.hpp"
#include "steam.hpp"
#include "u8.hpp"

namespace dsinst {

using nlohmann::json;

std::string timestamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
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

int compareVersions(const std::string& a, const std::string& b) {
    std::istringstream sa(a), sb(b);
    for (int i = 0; i < 4; ++i) {
        long x = 0, y = 0;
        std::string pa, pb;
        if (std::getline(sa, pa, '.')) x = std::strtol(pa.c_str(), nullptr, 10);
        if (std::getline(sb, pb, '.')) y = std::strtol(pb.c_str(), nullptr, 10);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

namespace {
fs::path manifestPath(const fs::path& game) { return game / "dscoop" / "install.json"; }
fs::path chainPath(const fs::path& game) { return game / "dscoop" / "chain" / "version_original.dll"; }

std::optional<json> readJson(const fs::path& p) {
    const std::string text = steam::readText(p);
    if (text.empty()) return std::nullopt;
    try { return json::parse(text, nullptr, true, true); } catch (...) { return std::nullopt; }
}

bool writeFileAtomic(const fs::path& target, const void* data, size_t size, std::string* err) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".dscoop-tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { if (err) *err = "cannot write " + utf8(tmp); return false; }
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out) { if (err) *err = "disk full? cannot write " + utf8(tmp); return false; }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        // Windows refuses to replace a file that is loaded (game running) or read-only.
        fs::remove(tmp, ec);
        if (err) *err = "cannot replace " + utf8(target) + " — is the game still running?";
        return false;
    }
    return true;
}

bool writeJson(const fs::path& p, const json& j, std::string* err) {
    const std::string s = j.dump(2) + "\n";
    return writeFileAtomic(p, s.data(), s.size(), err);
}

/// Byte copy through writeFileAtomic. std::filesystem::copy_file(overwrite_existing) is broken in
/// MinGW's libstdc++ on Windows (fails with "File exists"), so we never rely on it.
bool copyFile(const fs::path& from, const fs::path& to, std::string* err) {
    std::ifstream in(from, std::ios::binary);
    if (!in) { if (err) *err = "cannot read " + utf8(from); return false; }
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return writeFileAtomic(to, data.data(), data.size(), err);
}

uintmax_t copyTree(const fs::path& from, const fs::path& to, std::string* err) {
    std::error_code ec;
    uintmax_t bytes = 0;
    fs::create_directories(to, ec);
    for (auto it = fs::recursive_directory_iterator(from, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), from, ec);
        if (ec) break;
        if (it->is_directory(ec)) { fs::create_directories(to / rel, ec); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::string cerr;
        if (!copyFile(it->path(), to / rel, &cerr)) { if (err) *err = cerr; continue; }
        bytes += it->file_size(ec);
    }
    return bytes;
}

std::string humanSize(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes / (1024 * 1024)) + " MB";
}

bool sameContent(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
    return steam::readText(a) == steam::readText(b);
}
}  // namespace

bool canWrite(const fs::path& dir, std::string* err) {
    const fs::path probe = dir / ".dscoop-write-test";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) { if (err) *err = "no write access to " + utf8(dir); return false; }
        out << "ok";
    }
    std::error_code ec;
    fs::remove(probe, ec);
    return true;
}

InstallState inspect(const fs::path& gameDir) {
    InstallState s;
    std::error_code ec;
    if (auto m = readJson(manifestPath(gameDir))) {
        s.installed = true;
        s.version = m->value("version", "");
    }
    const fs::path loader = gameDir / "version.dll";
    s.loaderPresent = fs::is_regular_file(loader, ec);
    if (s.loaderPresent) {
        s.loaderIsOurs = pe::isDsLoader(loader);
        s.foreignLoader = !s.loaderIsOurs;
    }
    s.chainedForeign = fs::is_regular_file(chainPath(gameDir), ec);
    return s;
}

Result install(const InstallOptions& o, const Logger& log) {
    Result r;
    std::error_code ec;
    auto say = [&](const std::string& s) { if (log) log(s); };
    if (o.gameDir.empty() || !fs::is_directory(o.gameDir, ec)) { r.error = "game folder not found"; return r; }
    std::string err;
    if (!canWrite(o.gameDir, &err)) { r.error = err; return r; }
    const fs::path root = o.gameDir / "dscoop";
    const InstallState before = inspect(o.gameDir);
    if (before.installed) say("found DSCoop " + before.version + ", updating to " + o.version);

    // 1. Saves first — before anything else touches the game.
    if (o.backupSaves) {
        const fs::path dest = root / "backups" / ("saves_install_" + timestamp());
        size_t copied = 0;
        uintmax_t bytes = 0;
        for (const auto& src : o.saveDirs) {
            if (!fs::is_directory(src, ec)) continue;
            std::string cerr;
            bytes += copyTree(src, dest / src.filename(), &cerr);
            if (!cerr.empty()) say("warning: save backup: " + cerr);
            ++copied;
        }
        if (copied) say("saves backed up (" + humanSize(bytes) + ") -> " + utf8(dest));
        else say("no save folders found to back up (fresh install of the game?)");
    }

    // 2. A foreign version.dll (another mod) is moved aside and chain-loaded by DSLoader.
    json manifest = readJson(manifestPath(o.gameDir)).value_or(json::object());
    const fs::path loader = o.gameDir / "version.dll";
    if (before.foreignLoader) {
        fs::create_directories(chainPath(o.gameDir).parent_path(), ec);
        if (fs::exists(chainPath(o.gameDir), ec) && !sameContent(loader, chainPath(o.gameDir))) {
            const fs::path extra = root / "backups" / ("version_foreign_" + timestamp() + ".dll");
            fs::create_directories(extra.parent_path(), ec);
            fs::rename(loader, extra, ec);
            say("another mod's version.dll was moved to " + utf8(extra));
        } else {
            fs::rename(loader, chainPath(o.gameDir), ec);
            if (ec) { r.error = "cannot move existing version.dll: " + ec.message(); return r; }
            say("another mod already uses version.dll — moved it to dscoop/chain/, DSLoader will load it too");
            manifest["chained_foreign"] = true;
        }
    }

    // 3. Payload.
    json files = json::array();
    for (const auto& f : o.payload) {
        const fs::path target = o.gameDir / fromUtf8(f.relPath);
        if (!writeFileAtomic(target, f.data.data(), f.data.size(), &err)) { r.error = err; return r; }
        files.push_back(f.relPath);
        say("installed " + f.relPath + " (" + humanSize(f.data.size()) + ")");
    }

    // 4. Settings: create once, afterwards only nick / relay are changed on request.
    const fs::path cfgPath = root / "dscoop.json";
    json cfg = readJson(cfgPath).value_or(json::object());
    if (!o.nick.empty()) cfg["nick"] = o.nick;
    else if (!o.defaultNick.empty() && (!cfg.contains("nick") || cfg.value("nick", "").empty() || cfg.value("nick", "") == "Porter"))
        cfg["nick"] = o.defaultNick;
    if (!o.relayHost.empty()) cfg["relay"] = {{"host", o.relayHost}, {"control_port", o.relayPort}};
    else if (!cfg.contains("relay")) cfg["relay"] = {{"host", ""}, {"control_port", o.relayPort}};
    if (!writeJson(cfgPath, cfg, &err)) { r.error = err; return r; }

    // 5. Keep a copy of the installer for "Uninstall" in Windows settings.
    if (!o.installerExe.empty() && fs::is_regular_file(o.installerExe, ec)) {
        const fs::path dst = root / "DSCoopSetup.exe";
        if (!fs::equivalent(o.installerExe, dst, ec)) {
            std::string cerr;
            if (!copyFile(o.installerExe, dst, &cerr)) say("warning: could not keep a copy of the installer: " + cerr);
        }
    }

    for (const char* d : {"logs", "cache", "dumps", "backups", "reports"}) fs::create_directories(root / d, ec);

    manifest["version"] = o.version;
    manifest["installed_at"] = timestamp();
    manifest["files"] = files;
    if (!manifest.contains("chained_foreign")) manifest["chained_foreign"] = false;
    if (!writeJson(manifestPath(o.gameDir), manifest, &err)) { r.error = err; return r; }
    say("DSCoop " + o.version + " installed into " + utf8(o.gameDir));
    r.ok = true;
    return r;
}

Result uninstall(const fs::path& gameDir, bool removeUserData, const Logger& log) {
    Result r;
    std::error_code ec;
    auto say = [&](const std::string& s) { if (log) log(s); };
    if (!fs::is_directory(gameDir, ec)) { r.error = "game folder not found"; return r; }
    const fs::path root = gameDir / "dscoop";
    const auto manifest = readJson(manifestPath(gameDir));
    std::vector<std::string> files;
    if (manifest && manifest->contains("files"))
        for (const auto& f : (*manifest)["files"]) if (f.is_string()) files.push_back(f.get<std::string>());
    if (files.empty()) files = {"version.dll", "dscoop/plugins/DSCoop.dll", "dscoop/data/offsets.json"};

    for (const auto& rel : files) {
        const fs::path p = gameDir / fromUtf8(rel);
        if (!fs::exists(p, ec)) continue;
        if (rel == "version.dll" && !pe::isDsLoader(p)) { say("version.dll is not ours anymore — left untouched"); continue; }
        fs::remove(p, ec);
        if (ec) { r.error = "cannot remove " + utf8(p) + " — is the game still running?"; return r; }
        say("removed " + rel);
    }
    // Restore a displaced foreign version.dll.
    if (fs::is_regular_file(chainPath(gameDir), ec) && !fs::exists(gameDir / "version.dll", ec)) {
        fs::rename(chainPath(gameDir), gameDir / "version.dll", ec);
        if (!ec) say("restored the other mod's version.dll");
    }
    fs::remove(manifestPath(gameDir), ec);
    for (const char* d : {"plugins", "data", "chain", "cache"}) {
        if (fs::is_directory(root / d, ec) && fs::is_empty(root / d, ec)) fs::remove(root / d, ec);
    }
    fs::remove_all(root / "cache", ec);
    if (removeUserData) {
        for (const char* d : {"logs", "dumps", "reports"}) fs::remove_all(root / d, ec);
        fs::remove(root / "dscoop.json", ec);
        fs::remove(root / "dsloader.json", ec);
        say("settings and logs removed (save backups in dscoop/backups were kept)");
    } else {
        say("kept dscoop/ with your settings, logs and save backups");
    }
    r.ok = true;
    return r;
}

}  // namespace dsinst
