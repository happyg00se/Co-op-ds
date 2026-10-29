#pragma once
// UTF-8 <-> std::filesystem::path without the C++20 char8_t friction (and without the deprecated u8path).
#include <filesystem>
#include <string>

namespace dsinst {
inline std::string utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
inline std::filesystem::path fromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
}  // namespace dsinst
