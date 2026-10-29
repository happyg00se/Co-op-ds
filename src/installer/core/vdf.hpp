#pragma once
// Minimal parser for Valve KeyValues text (libraryfolders.vdf, appmanifest_*.acf, loginusers.vdf).
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dsinst::vdf {

struct Node {
    std::string key;
    std::string value;          // for leaf nodes
    std::vector<Node> children; // for object nodes
    bool isObject = false;

    /// Case-insensitive child lookup (Steam is inconsistent: "LibraryFolders" vs "libraryfolders").
    const Node* find(std::string_view k) const;
    std::string get(std::string_view k, std::string def = {}) const;
};

/// Parse a whole file. The returned root is an object whose children are the top-level keys.
std::optional<Node> parse(std::string_view text, std::string* err = nullptr);

}  // namespace dsinst::vdf
