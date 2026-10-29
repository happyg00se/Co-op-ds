#pragma once
// Read the export names of a PE file from disk without loading it (used to tell our DSLoader
// version.dll apart from a foreign proxy DLL another mod might have installed).
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dsinst::pe {

std::optional<std::vector<std::string>> exportNames(std::span<const uint8_t> image);
std::optional<std::vector<std::string>> exportNamesOfFile(const std::filesystem::path& file);

/// True if the file exports DSLoader_GetVersion.
bool isDsLoader(const std::filesystem::path& file);
bool isDsLoader(std::span<const uint8_t> image);

}  // namespace dsinst::pe
