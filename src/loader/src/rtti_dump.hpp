#pragma once
#include <string>

#include <nlohmann/json.hpp>

#include "game_module.hpp"

namespace dsloader {

struct RttiDumpResult {
    bool ok = false;
    size_t classCount = 0;
    std::string path;
    std::string error;
};

/// Scan the live module for Decima RTTI class records using offsets.json "rtti.layout" and write
/// dscoop/dumps/rtti_<build>.json. Refuses when layout.verified == false unless `force`.
RttiDumpResult dumpRtti(const GameModule& game, const nlohmann::json& offsets, const std::string& dumpsDir, bool force);

}  // namespace dsloader
