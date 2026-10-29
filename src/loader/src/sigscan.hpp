#pragma once
// Signature scanner + offsets.json entry resolution. Pure functions over byte ranges so the
// same code runs in the game process (live module) and in unit tests (synthetic buffers).
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace dsloader::sig {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;  // true = byte must match, false = wildcard
    size_t size() const { return bytes.size(); }
};

/// IDA-style: "48 8B 05 ?? ?? ?? ?? 48 85 C0" — wildcards are "?" or "??". Returns nullopt on syntax error.
std::optional<Pattern> parsePattern(std::string_view text);

/// A readable memory range with its virtual base address.
struct Region {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uintptr_t base = 0;
    bool contains(uintptr_t addr, size_t len = 1) const { return addr >= base && addr + len <= base + size; }
    const uint8_t* ptr(uintptr_t addr) const { return data + (addr - base); }
};

std::vector<uintptr_t> findAll(const Region& r, const Pattern& p, size_t maxResults = 16);

/// Strict: exactly one match. `err` explains 0 or >1.
std::optional<uintptr_t> findUnique(const Region& r, const Pattern& p, std::string* err = nullptr);

/// One entry of offsets.json "signatures":
/// {
///   "pattern": "E8 ?? ?? ?? ?? 48 8B D8",  // required
///   "offset": 0,                           // added to the match address first
///   "rip": { "disp": 1, "len": 5 },        // optional: resolve RIP-relative operand: target = addr + len + int32@(addr+disp)
///   "deref": 0,                            // optional: how many times to dereference the final address (needs live memory)
///   "required": true,                      // loader refuses to continue when a required sig is missing
///   "note": "what this is, how it was found"
/// }
struct RipRel { int32_t disp = 0; int32_t len = 0; };
struct SigEntry {
    std::string name;
    std::string pattern;
    int32_t offset = 0;
    std::optional<RipRel> rip;
    bool required = false;
    std::string note;
};

std::vector<SigEntry> loadSignatures(const nlohmann::json& offsets, std::string* err = nullptr);

/// Resolve one entry against a module image. Reads the RIP displacement from the same region.
std::optional<uintptr_t> resolve(const Region& module, const SigEntry& e, std::string* err = nullptr);

/// FNV-1a 32 — used for the game build id and cache keys.
uint32_t fnv1a32(std::span<const uint8_t> data, uint32_t seed = 2166136261u);

}  // namespace dsloader::sig
