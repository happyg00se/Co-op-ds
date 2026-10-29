#include "rtti_dump.hpp"

#include <filesystem>
#include <fstream>

#include "dscommon/log.hpp"
#include "rtti_scan.hpp"

namespace dsloader {

RttiDumpResult dumpRtti(const GameModule& game, const nlohmann::json& offsets, const std::string& dumpsDir, bool force) {
    RttiDumpResult res;
    rtti::Layout layout;
    if (offsets.contains("rtti") && offsets["rtti"].is_object()) layout = rtti::Layout::fromJson(offsets["rtti"].value("layout", nlohmann::json::object()));
    if (!layout.verified && !force) {
        res.error = "rtti.layout.verified is false in offsets.json — verify the layout (recon guide, step 6) or set dsloader.json rtti_force_unverified=true";
        return res;
    }
    const Section* data = game.section(".data");
    const Section* rdata = game.section(".rdata");
    if (!data) { res.error = "no .data section"; return res; }
    rtti::MemoryView mem{game.base(), reinterpret_cast<const uint8_t*>(game.base()), game.size()};
    rtti::Range dataRange{data->begin, data->begin + data->size};
    // Names may live in .rdata or .data depending on the build: accept both.
    rtti::Range rdataRange = rdata ? rtti::Range{std::min(rdata->begin, data->begin), std::max(rdata->begin + rdata->size, data->begin + data->size)} : dataRange;

    DSLOG_INFO("rtti: scanning .data ({} KB) for class records...", data->size / 1024);
    auto classes = rtti::scanClasses(mem, layout, dataRange, rdataRange, [](size_t n) { DSLOG_DEBUG("rtti: {} classes so far", n); (void)n; });
    rtti::resolveTypeNames(mem, layout, classes, rdataRange);
    res.classCount = classes.size();
    if (classes.empty()) {
        res.error = "no RTTI class records found — layout offsets are probably wrong for this build";
        return res;
    }
    std::error_code ec;
    std::filesystem::create_directories(dumpsDir, ec);
    const auto path = std::filesystem::path(dumpsDir) / ("rtti_" + std::to_string(game.buildId()) + ".json");
    std::ofstream out(path, std::ios::trunc);
    if (!out) { res.error = "cannot write " + path.string(); return res; }
    out << rtti::toJson(classes, layout, game.buildId(), game.base()).dump(1);
    res.ok = true;
    res.path = path.string();
    DSLOG_INFO("rtti: {} classes written to {}", classes.size(), res.path);
    return res;
}

}  // namespace dsloader
