#include "sigscan.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace dsloader::sig {

std::optional<Pattern> parsePattern(std::string_view text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        const std::string_view tok = text.substr(i, j - i);
        i = j;
        if (tok == "?" || tok == "??" || tok == "**") {
            p.bytes.push_back(0);
            p.mask.push_back(false);
            continue;
        }
        if (tok.size() != 2 || !std::isxdigit(static_cast<unsigned char>(tok[0])) || !std::isxdigit(static_cast<unsigned char>(tok[1])))
            return std::nullopt;
        auto hex = [](char c) -> uint8_t {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return static_cast<uint8_t>(c >= 'A' ? c - 'A' + 10 : c - '0');
        };
        p.bytes.push_back(static_cast<uint8_t>((hex(tok[0]) << 4) | hex(tok[1])));
        p.mask.push_back(true);
    }
    if (p.bytes.empty()) return std::nullopt;
    if (std::none_of(p.mask.begin(), p.mask.end(), [](bool b) { return b; })) return std::nullopt;  // all wildcards
    return p;
}

std::vector<uintptr_t> findAll(const Region& r, const Pattern& p, size_t maxResults) {
    std::vector<uintptr_t> out;
    if (!r.data || p.size() == 0 || r.size < p.size()) return out;
    // Anchor on the first fixed byte to skip quickly with memchr.
    size_t first = 0;
    while (first < p.size() && !p.mask[first]) ++first;
    const uint8_t anchor = p.bytes[first];
    const size_t last = r.size - p.size();
    size_t pos = 0;
    while (pos <= last) {
        const void* hit = std::memchr(r.data + pos + first, anchor, last - pos + 1);
        if (!hit) break;
        const size_t cand = static_cast<size_t>(static_cast<const uint8_t*>(hit) - r.data) - first;
        bool ok = true;
        for (size_t k = 0; k < p.size(); ++k) {
            if (p.mask[k] && r.data[cand + k] != p.bytes[k]) { ok = false; break; }
        }
        if (ok) {
            out.push_back(r.base + cand);
            if (out.size() >= maxResults) break;
        }
        pos = cand + 1;
    }
    return out;
}

std::optional<uintptr_t> findUnique(const Region& r, const Pattern& p, std::string* err) {
    auto hits = findAll(r, p, 2);
    if (hits.empty()) { if (err) *err = "no match"; return std::nullopt; }
    if (hits.size() > 1) { if (err) *err = "ambiguous: more than one match"; return std::nullopt; }
    return hits[0];
}

std::vector<SigEntry> loadSignatures(const nlohmann::json& offsets, std::string* err) {
    std::vector<SigEntry> out;
    if (!offsets.contains("signatures") || !offsets["signatures"].is_object()) {
        if (err) *err = "offsets.json: missing 'signatures' object";
        return out;
    }
    for (auto it = offsets["signatures"].begin(); it != offsets["signatures"].end(); ++it) {
        const auto& v = it.value();
        if (!v.is_object()) continue;
        SigEntry e;
        e.name = it.key();
        e.pattern = v.value("pattern", "");
        e.offset = v.value("offset", 0);
        e.required = v.value("required", false);
        e.note = v.value("note", "");
        if (v.contains("rip") && v["rip"].is_object()) {
            RipRel rr;
            rr.disp = v["rip"].value("disp", 0);
            rr.len = v["rip"].value("len", 0);
            e.rip = rr;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::optional<uintptr_t> resolve(const Region& module, const SigEntry& e, std::string* err) {
    if (e.pattern.empty()) { if (err) *err = "empty pattern (not yet found by recon)"; return std::nullopt; }
    auto pat = parsePattern(e.pattern);
    if (!pat) { if (err) *err = "bad pattern syntax"; return std::nullopt; }
    auto hit = findUnique(module, *pat, err);
    if (!hit) return std::nullopt;
    uintptr_t addr = *hit + static_cast<intptr_t>(e.offset);
    if (e.rip) {
        const uintptr_t dispAddr = addr + static_cast<intptr_t>(e.rip->disp);
        if (!module.contains(dispAddr, 4)) { if (err) *err = "rip displacement outside module"; return std::nullopt; }
        int32_t disp = 0;
        std::memcpy(&disp, module.ptr(dispAddr), 4);
        addr = addr + static_cast<uintptr_t>(e.rip->len) + static_cast<intptr_t>(disp);
    }
    if (!module.contains(addr)) { if (err) *err = "resolved address outside module"; return std::nullopt; }
    return addr;
}

uint32_t fnv1a32(std::span<const uint8_t> data, uint32_t seed) {
    uint32_t h = seed;
    for (uint8_t b : data) { h ^= b; h *= 16777619u; }
    return h;
}

}  // namespace dsloader::sig
