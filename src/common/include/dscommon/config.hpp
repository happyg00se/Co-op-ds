#pragma once
// JSON config helpers: load with defaults, write back a pretty file so users can edit it.
#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace dscommon::config {

using json = nlohmann::json;

/// Reads a JSON file. Returns nullopt (and fills `error`) on missing file or parse error.
std::optional<json> load(const std::filesystem::path& file, std::string* error = nullptr);

/// Writes pretty JSON atomically (tmp + rename). Returns false on failure.
bool save(const std::filesystem::path& file, const json& j, std::string* error = nullptr);

/// Loads `file`; if missing, writes `defaults` to disk and returns them. Missing keys are
/// filled from `defaults` (shallow merge for objects, one level deep).
json loadOrCreate(const std::filesystem::path& file, const json& defaults);

/// Deep-merge `patch` into `base` (objects recursively, everything else overwritten).
void mergeDefaults(json& target, const json& defaults);

}  // namespace dscommon::config
