#include "offsets.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace dsloader {

bool Offsets::load(const std::string& path, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (err) *err = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << in.rdbuf();
    rawText_ = ss.str();
    try {
        json_ = nlohmann::json::parse(rawText_, nullptr, true, true);
    } catch (const std::exception& e) {
        if (err) *err = std::string("offsets.json parse error: ") + e.what();
        return false;
    }
    std::string e;
    entries_ = sig::loadSignatures(json_, &e);
    if (!e.empty() && err) *err = e;
    return true;
}

bool Offsets::resolveAll(const GameModule& game, const std::string& cacheDir) {
    results_.clear();
    byName_.clear();
    // Cache: { "build_id": N, "patterns": { name: { "pattern": "...", "rva": N } } }
    const std::filesystem::path cacheFile = std::filesystem::path(cacheDir) / ("sigcache_" + std::to_string(game.buildId()) + ".json");
    nlohmann::json cache;
    {
        std::ifstream in(cacheFile);
        if (in) { try { cache = nlohmann::json::parse(in); } catch (...) { cache = nlohmann::json(); } }
    }
    if (!cache.is_object() || cache.value("build_id", 0u) != game.buildId()) cache = {{"build_id", game.buildId()}, {"patterns", nlohmann::json::object()}};
    auto& cp = cache["patterns"];

    const auto codeRegions = game.codeRegions();
    const auto image = game.imageRegion();
    bool allRequired = true;
    bool cacheDirty = false;

    for (const auto& e : entries_) {
        ResolvedSig r;
        r.name = e.name;
        r.required = e.required;
        if (e.pattern.empty()) {
            r.error = "pattern not yet found (recon pending)";
            results_.push_back(r);
            if (e.required) allRequired = false;
            continue;
        }
        // Cache hit only if the pattern text is unchanged for this build.
        if (cp.contains(e.name) && cp[e.name].value("pattern", "") == e.pattern && cp[e.name].value("offset", 0) == e.offset) {
            r.address = game.base() + cp[e.name].value("rva", 0ull);
            r.fromCache = true;
        } else {
            std::string err;
            std::optional<uintptr_t> addr;
            // Scan executable sections first; fall back to the whole image for data patterns.
            for (const auto& region : codeRegions) {
                addr = sig::resolve(region, e, &err);
                if (addr) break;
                if (err.find("ambiguous") != std::string::npos) break;
            }
            if (!addr && err.find("ambiguous") == std::string::npos) addr = sig::resolve(image, e, &err);
            if (addr) {
                r.address = *addr;
                cp[e.name] = {{"pattern", e.pattern}, {"offset", e.offset}, {"rva", *addr - game.base()}};
                cacheDirty = true;
            } else {
                r.error = err;
                if (e.required) allRequired = false;
            }
        }
        if (r.address) byName_[e.name] = r.address;
        results_.push_back(r);
    }
    if (cacheDirty) {
        std::error_code ec;
        std::filesystem::create_directories(cacheDir, ec);
        std::ofstream out(cacheFile, std::ios::trunc);
        if (out) out << cache.dump(2);
    }
    return allRequired;
}

uintptr_t Offsets::resolve(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? 0 : it->second;
}

size_t Offsets::resolvedCount() const {
    size_t n = 0;
    for (const auto& r : results_) if (r.address) ++n;
    return n;
}
size_t Offsets::pendingCount() const {
    size_t n = 0;
    for (const auto& r : results_) if (!r.address && r.error.find("recon pending") != std::string::npos) ++n;
    return n;
}

}  // namespace dsloader
