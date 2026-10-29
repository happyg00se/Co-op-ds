#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

#include "install.hpp"
#include "minitest.hpp"
#include "pe.hpp"
#include "steam.hpp"
#include "vdf.hpp"

namespace fs = std::filesystem;
using namespace dsinst;

namespace {
fs::path tempDir(const char* tag) {
    std::random_device rd;
    fs::path p = fs::temp_directory_path() / ("dscoop_test_" + std::string(tag) + "_" + std::to_string(rd()));
    fs::create_directories(p);
    return p;
}
void writeText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
std::string readAll(const fs::path& p) { return steam::readText(p); }

/// Minimal PE32+ image with one section holding an export directory with the given names.
std::vector<uint8_t> makePe(const std::vector<std::string>& exports) {
    std::vector<uint8_t> f(0x400, 0);
    auto put16 = [&](size_t o, uint16_t v) { std::memcpy(&f[o], &v, 2); };
    auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&f[o], &v, 4); };
    put16(0, 0x5A4D);
    put32(0x3C, 0x40);
    put32(0x40, 0x00004550);
    const size_t fh = 0x44;
    put16(fh + 0, 0x8664);
    put16(fh + 2, 1);       // sections
    put16(fh + 16, 0xF0);   // SizeOfOptionalHeader
    const size_t opt = fh + 20;
    put16(opt, 0x20B);
    put32(opt + 108, 16);   // NumberOfRvaAndSizes
    const size_t sec = opt + 0xF0;
    std::memcpy(&f[sec], ".edata", 6);
    put32(sec + 8, 0x200);   // VirtualSize
    put32(sec + 12, 0x1000); // VirtualAddress
    put32(sec + 16, 0x200);  // SizeOfRawData
    put32(sec + 20, 0x200);  // PointerToRawData
    // export directory at rva 0x1000 (file 0x200)
    const uint32_t n = static_cast<uint32_t>(exports.size());
    const uint32_t namesRva = 0x1000 + 40;
    uint32_t strRva = namesRva + 4 * n;
    put32(0x200 + 24, n);
    put32(0x200 + 32, namesRva);
    for (uint32_t i = 0; i < n; ++i) {
        put32(0x200 + 40 + 4 * i, strRva);
        std::memcpy(&f[0x200 + (strRva - 0x1000)], exports[i].c_str(), exports[i].size() + 1);
        strRva += static_cast<uint32_t>(exports[i].size() + 1);
    }
    put32(opt + 112, 0x1000);  // export dir rva
    put32(opt + 116, 0x100);
    return f;
}

struct FakeSteam {
    fs::path root, steam, lib2, game;
    FakeSteam() {
        root = tempDir("steam");
        steam = root / "Steam";
        lib2 = root / "SteamLibrary";
        // New-format libraryfolders with escaped Windows-style path for library 1.
        writeText(steam / "steamapps" / "libraryfolders.vdf",
                  "\"libraryfolders\"\n{\n"
                  "  \"contentstatsid\" \"123\"\n"
                  "  \"0\" { \"path\" \"" + steam.generic_string() + "\" \"apps\" { \"228980\" \"1\" } }\n"
                  "  \"1\" { \"path\" \"" + lib2.generic_string() + "\" // comment\n \"apps\" { \"1850570\" \"80000000000\" } }\n"
                  "}\n");
        writeText(lib2 / "steamapps" / "appmanifest_1850570.acf",
                  "\"AppState\"\n{\n \"appid\" \"1850570\"\n \"name\" \"DEATH STRANDING DIRECTOR'S CUT\"\n \"installdir\" \"DEATH STRANDING DIRECTORS CUT\"\n}\n");
        game = lib2 / "steamapps" / "common" / "DEATH STRANDING DIRECTORS CUT";
        writeText(game / "ds.exe", std::string(4096, 'x'));
        writeText(game / "CrashReporter.exe", std::string(100000, 'x'));
        writeText(steam / "config" / "loginusers.vdf",
                  "\"users\" { \"7656119800000001\" { \"AccountName\" \"old\" \"PersonaName\" \"OldName\" \"MostRecent\" \"0\" }"
                  " \"7656119800000002\" { \"AccountName\" \"sam\" \"PersonaName\" \"Sam \\\"Porter\\\" B\" \"MostRecent\" \"1\" } }");
    }
    ~FakeSteam() { std::error_code ec; fs::remove_all(root, ec); }
};

std::vector<PayloadFile> payload(const std::string& tag) {
    const auto pe = makePe({"GetFileVersionInfoA", "DSLoader_GetVersion", "VerQueryValueW"});
    std::vector<uint8_t> plugin(tag.begin(), tag.end());
    std::string offs = "{\"schema_version\":1,\"tag\":\"" + tag + "\"}";
    return {{"version.dll", pe}, {"dscoop/plugins/DSCoop.dll", plugin}, {"dscoop/data/offsets.json", std::vector<uint8_t>(offs.begin(), offs.end())}};
}
}  // namespace

TEST(vdf_parse_basic) {
    auto r = vdf::parse("// hi\n\"A\" { \"b\" \"c\\\\d\" \"E\" { \"f\" \"1\" } [$WIN32] \"g\" \"x\\\"y\" }");
    CHECK(r.has_value());
    const auto* a = r->find("a");
    CHECK(a && a->isObject);
    CHECK_EQ(a->get("B"), std::string("c\\d"));
    CHECK_EQ(a->find("e")->get("f"), std::string("1"));
    CHECK_EQ(a->get("g"), std::string("x\"y"));
    CHECK(!vdf::parse("\"a\" { \"b\" ").has_value());
    CHECK(!vdf::parse("\"a\" }").has_value());
}

TEST(steam_finds_game_in_second_library) {
    FakeSteam s;
    auto libs = steam::libraryFolders(s.steam);
    CHECK_EQ(libs.size(), size_t(2));
    auto g = steam::findDeathStranding(s.steam);
    CHECK(g.has_value());
    if (g) {
        CHECK(g->directorsCut());
        CHECK(fs::equivalent(g->gameDir, s.game));
        CHECK_EQ(g->exe.filename().string(), std::string("ds.exe"));  // known name beats the bigger crash reporter
        CHECK_EQ(g->name, std::string("DEATH STRANDING DIRECTOR'S CUT"));
    }
    auto persona = steam::mostRecentPersona(s.steam);
    CHECK(persona.has_value());
    CHECK_EQ(persona.value_or(""), std::string("Sam \"Porter\" B"));
}

TEST(steam_old_libraryfolders_format) {
    FakeSteam s;
    writeText(s.steam / "steamapps" / "libraryfolders.vdf",
              "\"LibraryFolders\" { \"TimeNextStatsReport\" \"1\" \"1\" \"" + s.lib2.generic_string() + "\" }");
    auto g = steam::findDeathStranding(s.steam);
    CHECK(g.has_value());
}

TEST(steam_falls_back_to_largest_exe) {
    FakeSteam s;
    fs::remove(s.game / "ds.exe");
    writeText(s.game / "Game.exe", std::string(50000, 'y'));
    CHECK_EQ(steam::findGameExe(s.game).filename().string(), std::string("Game.exe"));  // crash reporter is skipped
}

TEST(pe_exports) {
    auto pe = makePe({"Alpha", "DSLoader_GetVersion"});
    auto names = pe::exportNames(pe);
    CHECK(names.has_value());
    CHECK_EQ(names->size(), size_t(2));
    CHECK(pe::isDsLoader(std::span<const uint8_t>(pe)));
    CHECK(!pe::isDsLoader(std::span<const uint8_t>(makePe({"GetFileVersionInfoA"}))));
    std::vector<uint8_t> junk(100, 0x41);
    CHECK(!pe::exportNames(junk).has_value());
    // Optional: a real version.dll from the MinGW build.
    if (const char* real = std::getenv("DSCOOP_TEST_VERSION_DLL")) {
        CHECK(pe::isDsLoader(fs::path(real)));
        auto rn = pe::exportNamesOfFile(real);
        CHECK(rn && rn->size() == 17);
    }
}

TEST(install_update_uninstall_with_foreign_mod) {
    FakeSteam s;
    const fs::path saves = s.root / "LocalAppData" / "KojimaProductions";
    writeText(saves / "DeathStrandingDC" / "76561198" / "checkpoint.dat", "SAVE-DATA");
    // Another mod already uses version.dll (not ours: no DSLoader_GetVersion export).
    const auto foreign = makePe({"GetFileVersionInfoA"});
    { std::ofstream(s.game / "version.dll", std::ios::binary).write(reinterpret_cast<const char*>(foreign.data()), static_cast<std::streamsize>(foreign.size())); }

    auto st0 = inspect(s.game);
    CHECK(!st0.installed); CHECK(st0.foreignLoader);

    InstallOptions o;
    o.gameDir = s.game;
    o.payload = payload("v1");
    o.version = "0.2.0";
    o.nick = "Sam";
    o.relayHost = "relay.example.com";
    o.relayPort = 27001;
    o.saveDirs = {saves};
    std::vector<std::string> lines;
    auto r = install(o, [&](const std::string& l) { lines.push_back(l); });
    CHECK(r.ok);
    if (!r.ok) std::printf("  error: %s\n", r.error.c_str());

    auto st1 = inspect(s.game);
    CHECK(st1.installed); CHECK_EQ(st1.version, std::string("0.2.0"));
    CHECK(st1.loaderIsOurs); CHECK(!st1.foreignLoader); CHECK(st1.chainedForeign);
    CHECK_EQ(readAll(s.game / "dscoop" / "plugins" / "DSCoop.dll"), std::string("v1"));
    CHECK(fs::exists(s.game / "dscoop" / "data" / "offsets.json"));
    const std::string cfg = readAll(s.game / "dscoop" / "dscoop.json");
    CHECK(cfg.find("\"nick\": \"Sam\"") != std::string::npos);
    CHECK(cfg.find("relay.example.com") != std::string::npos);
    CHECK(cfg.find("27001") != std::string::npos);
    // Saves copied.
    bool backupFound = false;
    for (const auto& e : fs::recursive_directory_iterator(s.game / "dscoop" / "backups"))
        if (e.path().filename() == "checkpoint.dat" && readAll(e.path()) == "SAVE-DATA") backupFound = true;
    CHECK(backupFound);
    for (const char* d : {"logs", "cache", "dumps", "reports"}) CHECK(fs::is_directory(s.game / "dscoop" / d));

    // Player tweaks a setting; an update must keep it and must not move our own loader into chain/.
    {
        std::string c = readAll(s.game / "dscoop" / "dscoop.json");
        c.insert(c.find('{') + 1, "\n  \"menu_key\": \"F7\",");
        writeText(s.game / "dscoop" / "dscoop.json", c);
    }
    o.payload = payload("v2");
    o.version = "0.2.1";
    o.nick.clear();
    o.relayHost.clear();
    o.backupSaves = false;
    r = install(o, nullptr);
    CHECK(r.ok);
    CHECK_EQ(inspect(s.game).version, std::string("0.2.1"));
    CHECK_EQ(readAll(s.game / "dscoop" / "plugins" / "DSCoop.dll"), std::string("v2"));
    const std::string cfg2 = readAll(s.game / "dscoop" / "dscoop.json");
    CHECK(cfg2.find("F7") != std::string::npos);
    CHECK(cfg2.find("\"nick\": \"Sam\"") != std::string::npos);
    CHECK(cfg2.find("relay.example.com") != std::string::npos);
    CHECK(readAll(s.game / "dscoop" / "chain" / "version_original.dll") == std::string(foreign.begin(), foreign.end()));

    // Uninstall: our files go, the other mod's version.dll comes back, user data stays.
    writeText(s.game / "dscoop" / "logs" / "dscoop.log", "log");
    r = uninstall(s.game, false, nullptr);
    CHECK(r.ok);
    CHECK(!fs::exists(s.game / "dscoop" / "plugins" / "DSCoop.dll"));
    CHECK(!fs::exists(s.game / "dscoop" / "install.json"));
    CHECK(readAll(s.game / "version.dll") == std::string(foreign.begin(), foreign.end()));
    CHECK(!fs::exists(s.game / "dscoop" / "chain" / "version_original.dll"));
    CHECK(fs::exists(s.game / "dscoop" / "logs" / "dscoop.log"));
    CHECK(fs::exists(s.game / "dscoop" / "dscoop.json"));
    CHECK(backupFound && fs::exists(s.game / "dscoop" / "backups"));
    auto st2 = inspect(s.game);
    CHECK(!st2.installed); CHECK(st2.foreignLoader);
}

TEST(clean_install_and_purge) {
    FakeSteam s;
    InstallOptions o;
    o.gameDir = s.game;
    o.payload = payload("v1");
    o.version = "0.2.0";
    o.backupSaves = false;
    CHECK(install(o, nullptr).ok);
    const std::string cfg = readAll(s.game / "dscoop" / "dscoop.json");
    CHECK(cfg.find("\"relay\"") != std::string::npos);  // relay block always present
    CHECK(inspect(s.game).loaderIsOurs);
    CHECK(uninstall(s.game, true, nullptr).ok);
    CHECK(!fs::exists(s.game / "version.dll"));  // nothing to restore
    CHECK(!fs::exists(s.game / "dscoop" / "dscoop.json"));
    CHECK(fs::exists(s.game / "ds.exe"));  // game untouched
}

TEST(uninstall_leaves_foreign_loader_alone) {
    FakeSteam s;
    InstallOptions o;
    o.gameDir = s.game;
    o.payload = payload("v1");
    o.version = "0.2.0";
    o.backupSaves = false;
    CHECK(install(o, nullptr).ok);
    // Someone replaced our version.dll with another mod's after we installed.
    const auto foreign = makePe({"Other"});
    std::ofstream(s.game / "version.dll", std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(foreign.data()), static_cast<std::streamsize>(foreign.size()));
    CHECK(uninstall(s.game, false, nullptr).ok);
    CHECK(fs::exists(s.game / "version.dll"));
}

TEST(steam_name_never_overrides_chosen_nick) {
    FakeSteam s;
    InstallOptions o;
    o.gameDir = s.game;
    o.payload = payload("v1");
    o.version = "0.2.0";
    o.backupSaves = false;
    o.defaultNick = "SteamName";
    CHECK(install(o, nullptr).ok);
    CHECK(readAll(s.game / "dscoop" / "dscoop.json").find("SteamName") != std::string::npos);  // first install: Steam name
    o.nick = "Chosen";
    CHECK(install(o, nullptr).ok);
    CHECK(uninstall(s.game, false, nullptr).ok);  // settings are kept on uninstall
    o.nick.clear();
    CHECK(install(o, nullptr).ok);                // reinstall later
    const std::string cfg = readAll(s.game / "dscoop" / "dscoop.json");
    CHECK(cfg.find("Chosen") != std::string::npos);
    CHECK(cfg.find("SteamName") == std::string::npos);
}

TEST(version_compare) {
    CHECK(compareVersions("0.2.0", "0.10.0") < 0);
    CHECK(compareVersions("1.0", "1.0.0") == 0);
    CHECK(compareVersions("0.2.1", "0.2.0") > 0);
}

int main() { return RUN_ALL_TESTS(); }
