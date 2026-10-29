#pragma once
// Loads data/offsets.json, resolves every signature against the live game module (with a
// per-build cache in dscoop/cache/) and answers resolve(name) for plugins.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "game_module.hpp"
#include "sigscan.hpp"

namespace dsloader {

struct ResolvedSig {
    std::string name;
    uintptr_t address = 0;  // 0 = unresolved
    bool required = false;
    bool fromCache = false;
    std::string error;
};

class Offsets {
public:
    bool load(const std::string& path, std::string* err);
    const nlohmann::json& json() const { return json_; }
    const std::string& rawText() const { return rawText_; }

    /// Resolve all signatures. Returns false if any *required* one is missing.
    bool resolveAll(const GameModule& game, const std::string& cacheDir);
    uintptr_t resolve(const std::string& name) const;
    const std::vector<ResolvedSig>& results() const { return results_; }
    size_t resolvedCount() const;
    size_t pendingCount() const;  // entries with empty patterns = recon still to do

private:
    nlohmann::json json_;
    std::string rawText_;
    std::vector<sig::SigEntry> entries_;
    std::vector<ResolvedSig> results_;
    std::map<std::string, uintptr_t> byName_;
};

}  // namespace dsloader
