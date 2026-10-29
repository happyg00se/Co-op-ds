#include "steam.hpp"
#include "u8.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "vdf.hpp"

namespace dsinst::steam {

std::string readText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

namespace {
bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec)) return fs::equivalent(a, b, ec);
    auto norm = [](fs::path p) {
        std::string s = p.lexically_normal().generic_string();
        while (s.size() > 1 && s.back() == '/') s.pop_back();
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return norm(a) == norm(b);
}
}  // namespace

std::vector<fs::path> libraryFolders(const fs::path& steamRoot) {
    std::vector<fs::path> out{steamRoot};
    for (const char* rel : {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"}) {
        const std::string text = readText(steamRoot / rel);
        if (text.empty()) continue;
        auto root = vdf::parse(text);
        if (!root) continue;
        const vdf::Node* lf = root->find("libraryfolders");
        if (!lf || !lf->isObject) continue;
        for (const auto& entry : lf->children) {
            if (entry.key.empty() || !std::isdigit(static_cast<unsigned char>(entry.key[0]))) continue;  // skip "contentstatsid" etc.
            fs::path p;
            if (entry.isObject) p = fromUtf8(entry.get("path"));  // new format
            else p = fromUtf8(entry.value);                       // old format: "1" "D:\\SteamLibrary"
            if (p.empty()) continue;
            if (std::none_of(out.begin(), out.end(), [&](const fs::path& q) { return samePath(p, q); })) out.push_back(p);
        }
        break;
    }
    return out;
}

fs::path findGameExe(const fs::path& gameDir) {
    std::error_code ec;
    for (const char* n : {"ds.exe", "DeathStranding.exe", "DS.exe"}) {
        if (fs::is_regular_file(gameDir / n, ec)) return gameDir / n;
    }
    fs::path best;
    uintmax_t bestSize = 0;
    for (const auto& e : fs::directory_iterator(gameDir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string ext = e.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".exe") continue;
        std::string stem = e.path().stem().string();
        for (auto& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (stem.find("crash") != std::string::npos || stem.find("unins") != std::string::npos || stem.find("redist") != std::string::npos) continue;
        const auto sz = e.file_size(ec);
        if (sz > bestSize) { bestSize = sz; best = e.path(); }
    }
    return best;
}

std::optional<GameInstall> findApp(const fs::path& steamRoot, const std::string& appId) {
    std::error_code ec;
    for (const auto& lib : libraryFolders(steamRoot)) {
        const fs::path manifest = lib / "steamapps" / ("appmanifest_" + appId + ".acf");
        const std::string text = readText(manifest);
        if (text.empty()) continue;
        auto root = vdf::parse(text);
        if (!root) continue;
        const vdf::Node* st = root->find("AppState");
        if (!st || !st->isObject) continue;
        const std::string installdir = st->get("installdir");
        if (installdir.empty()) continue;
        GameInstall g;
        g.appId = appId;
        g.name = st->get("name");
        g.library = lib;
        g.gameDir = lib / "steamapps" / "common" / fromUtf8(installdir);
        if (!fs::is_directory(g.gameDir, ec)) continue;
        g.exe = findGameExe(g.gameDir);
        return g;
    }
    return std::nullopt;
}

std::optional<GameInstall> findDeathStranding(const fs::path& steamRoot) {
    if (auto g = findApp(steamRoot, kAppDirectorsCut)) return g;
    return findApp(steamRoot, kAppOriginal);
}

std::optional<std::string> mostRecentPersona(const fs::path& steamRoot) {
    const std::string text = readText(steamRoot / "config" / "loginusers.vdf");
    if (text.empty()) return std::nullopt;
    auto root = vdf::parse(text);
    if (!root) return std::nullopt;
    const vdf::Node* users = root->find("users");
    if (!users || !users->isObject) return std::nullopt;
    const vdf::Node* pick = nullptr;
    for (const auto& u : users->children) {
        if (!u.isObject) continue;
        if (!pick) pick = &u;
        if (u.get("MostRecent") == "1" || u.get("mostrecent") == "1") { pick = &u; break; }
    }
    if (!pick) return std::nullopt;
    std::string name = pick->get("PersonaName");
    if (name.empty()) return std::nullopt;
    return name;
}

}  // namespace dsinst::steam
