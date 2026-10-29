#include "rtti_scan.hpp"

#include <algorithm>
#include <cstring>

namespace dsloader::rtti {

namespace {
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '<' || c == '>' || c == ','; }
bool looksLikeTypeName(const std::string& s) {
    if (s.size() < 2 || s.size() > 200) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return isIdentChar(c) || c == ' ' || c == '*' || c == '&'; });
}
}  // namespace

std::optional<std::string> MemoryView::readCString(uintptr_t addr, size_t maxLen) const {
    if (!contains(addr)) return std::nullopt;
    std::string s;
    for (size_t i = 0; i < maxLen && contains(addr + i); ++i) {
        const char c = static_cast<char>(data[addr - base + i]);
        if (c == 0) return s;
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) return std::nullopt;
        s.push_back(c);
    }
    return std::nullopt;  // unterminated within maxLen
}

#define DSL_GET(j, field) if ((j).contains(#field)) l.field = (j)[#field].get<decltype(l.field)>();
Layout Layout::fromJson(const nlohmann::json& j) {
    Layout l;
    if (!j.is_object()) return l;
    DSL_GET(j, kindOffset) DSL_GET(j, kindClass) DSL_GET(j, kindEnum) DSL_GET(j, kindPrimitive) DSL_GET(j, kindMax)
    DSL_GET(j, inheritanceCountOffset) DSL_GET(j, memberCountOffset) DSL_GET(j, sizeOffset) DSL_GET(j, nameOffset)
    DSL_GET(j, prevOffset) DSL_GET(j, nextOffset) DSL_GET(j, inheritanceTableOffset) DSL_GET(j, memberTableOffset)
    DSL_GET(j, headerSize) DSL_GET(j, inhEntrySize) DSL_GET(j, inhTypeOffset) DSL_GET(j, inhOffsetOffset)
    DSL_GET(j, memberEntrySize) DSL_GET(j, memberTypeOffset) DSL_GET(j, memberOffsetOffset) DSL_GET(j, memberFlagsOffset)
    DSL_GET(j, memberNameOffset) DSL_GET(j, maxMembers) DSL_GET(j, maxBases) DSL_GET(j, maxSize) DSL_GET(j, verified)
    return l;
}
#undef DSL_GET

#define DSL_PUT(field) j[#field] = field;
nlohmann::json Layout::toJson() const {
    nlohmann::json j;
    DSL_PUT(kindOffset) DSL_PUT(kindClass) DSL_PUT(kindEnum) DSL_PUT(kindPrimitive) DSL_PUT(kindMax)
    DSL_PUT(inheritanceCountOffset) DSL_PUT(memberCountOffset) DSL_PUT(sizeOffset) DSL_PUT(nameOffset)
    DSL_PUT(prevOffset) DSL_PUT(nextOffset) DSL_PUT(inheritanceTableOffset) DSL_PUT(memberTableOffset)
    DSL_PUT(headerSize) DSL_PUT(inhEntrySize) DSL_PUT(inhTypeOffset) DSL_PUT(inhOffsetOffset)
    DSL_PUT(memberEntrySize) DSL_PUT(memberTypeOffset) DSL_PUT(memberOffsetOffset) DSL_PUT(memberFlagsOffset)
    DSL_PUT(memberNameOffset) DSL_PUT(maxMembers) DSL_PUT(maxBases) DSL_PUT(maxSize) DSL_PUT(verified)
    return j;
}
#undef DSL_PUT

std::optional<ClassInfo> tryReadClass(const MemoryView& mem, const Layout& l, uintptr_t addr, const Range& dataRange, const Range& rdataRange) {
    if (!mem.contains(addr, static_cast<size_t>(l.headerSize))) return std::nullopt;
    const auto kind = mem.read<uint8_t>(addr + l.kindOffset);
    if (!kind || *kind != l.kindClass) return std::nullopt;

    const auto namePtr = mem.read<uint64_t>(addr + l.nameOffset);
    if (!namePtr || !rdataRange.contains(*namePtr)) return std::nullopt;
    const auto name = mem.readCString(*namePtr);
    if (!name || !looksLikeTypeName(*name)) return std::nullopt;

    const auto size = mem.read<uint32_t>(addr + l.sizeOffset);
    if (!size || *size > l.maxSize) return std::nullopt;

    const auto memberCount = mem.read<uint16_t>(addr + l.memberCountOffset);
    const auto baseCount = mem.read<uint8_t>(addr + l.inheritanceCountOffset);
    if (!memberCount || !baseCount || *memberCount > l.maxMembers || *baseCount > l.maxBases) return std::nullopt;

    const auto memberTable = mem.read<uint64_t>(addr + l.memberTableOffset);
    const auto baseTable = mem.read<uint64_t>(addr + l.inheritanceTableOffset);
    if (!memberTable || !baseTable) return std::nullopt;
    if (*memberCount > 0 && !dataRange.contains(*memberTable)) return std::nullopt;
    if (*baseCount > 0 && !dataRange.contains(*baseTable)) return std::nullopt;
    if (*memberCount == 0 && *memberTable != 0 && !dataRange.contains(*memberTable)) return std::nullopt;

    // Linked-list pointers must be null or point into data.
    for (int off : {l.prevOffset, l.nextOffset}) {
        if (off < 0) continue;
        const auto p = mem.read<uint64_t>(addr + off);
        if (!p) return std::nullopt;
        if (*p != 0 && !dataRange.contains(*p)) return std::nullopt;
    }

    ClassInfo ci;
    ci.address = addr;
    ci.name = *name;
    ci.size = *size;
    ci.kind = *kind;

    for (uint32_t i = 0; i < *baseCount; ++i) {
        const uintptr_t e = *baseTable + static_cast<uintptr_t>(i) * l.inhEntrySize;
        BaseInfo b;
        const auto t = mem.read<uint64_t>(e + l.inhTypeOffset);
        const auto o = mem.read<uint32_t>(e + l.inhOffsetOffset);
        if (!t || !o || !dataRange.contains(*t)) return std::nullopt;
        b.typeRtti = *t; b.offset = *o;
        ci.bases.push_back(b);
    }
    for (uint32_t i = 0; i < *memberCount; ++i) {
        const uintptr_t e = *memberTable + static_cast<uintptr_t>(i) * l.memberEntrySize;
        MemberInfo m;
        const auto t = mem.read<uint64_t>(e + l.memberTypeOffset);
        const auto o = mem.read<uint16_t>(e + l.memberOffsetOffset);
        const auto f = mem.read<uint16_t>(e + l.memberFlagsOffset);
        const auto n = mem.read<uint64_t>(e + l.memberNameOffset);
        if (!t || !o || !f || !n) return std::nullopt;
        // Decima member tables contain "category" rows with a null type and a name; keep them.
        if (*t != 0 && !dataRange.contains(*t)) return std::nullopt;
        if (*n != 0) {
            if (!rdataRange.contains(*n)) return std::nullopt;
            auto nm = mem.readCString(*n);
            if (!nm) return std::nullopt;
            m.name = *nm;
        }
        m.typeRtti = *t; m.offset = *o; m.flags = *f;
        ci.members.push_back(std::move(m));
    }
    return ci;
}

std::vector<ClassInfo> scanClasses(const MemoryView& mem, const Layout& l, const Range& dataRange, const Range& rdataRange,
                                   std::function<void(size_t)> progress) {
    std::vector<ClassInfo> out;
    const uintptr_t start = (dataRange.begin + 7) & ~uintptr_t{7};
    for (uintptr_t a = start; a + static_cast<uintptr_t>(l.headerSize) <= dataRange.end; a += 8) {
        if (auto ci = tryReadClass(mem, l, a, dataRange, rdataRange)) {
            out.push_back(std::move(*ci));
            if (progress && (out.size() % 500) == 0) progress(out.size());
        }
    }
    return out;
}

void resolveTypeNames(const MemoryView& mem, const Layout& l, std::vector<ClassInfo>& classes, const Range& rdataRange) {
    auto nameOf = [&](uintptr_t rtti) -> std::string {
        if (rtti == 0) return "";
        const auto kind = mem.read<uint8_t>(rtti + l.kindOffset);
        if (!kind || *kind >= l.kindMax) return "?";
        // Classes keep the name at nameOffset. Other kinds have different layouts; we try the class
        // slot first and fall back to a conservative scan of the first 0x40 bytes for a string pointer.
        if (*kind == l.kindClass) {
            if (auto p = mem.read<uint64_t>(rtti + l.nameOffset); p && rdataRange.contains(*p))
                if (auto s = mem.readCString(*p); s && looksLikeTypeName(*s)) return *s;
            return "?class";
        }
        for (int off = 8; off < 0x40; off += 8) {
            if (auto p = mem.read<uint64_t>(rtti + off); p && rdataRange.contains(*p))
                if (auto s = mem.readCString(*p); s && looksLikeTypeName(*s)) return *s;
        }
        return "?kind" + std::to_string(*kind);
    };
    for (auto& c : classes) {
        for (auto& b : c.bases) b.name = nameOf(b.typeRtti);
        for (auto& m : c.members) m.typeName = nameOf(m.typeRtti);
    }
}

nlohmann::json toJson(const std::vector<ClassInfo>& classes, const Layout& l, uint32_t gameBuild, uintptr_t moduleBase) {
    nlohmann::json j;
    j["format"] = 1;
    j["game_build"] = gameBuild;
    j["module_base"] = moduleBase;
    j["layout"] = l.toJson();
    j["class_count"] = classes.size();
    auto& arr = j["classes"] = nlohmann::json::array();
    for (const auto& c : classes) {
        nlohmann::json cj;
        cj["name"] = c.name;
        cj["rva"] = c.address - moduleBase;
        cj["size"] = c.size;
        auto& bases = cj["bases"] = nlohmann::json::array();
        for (const auto& b : c.bases) bases.push_back({{"name", b.name}, {"offset", b.offset}, {"rva", b.typeRtti ? b.typeRtti - moduleBase : 0}});
        auto& members = cj["members"] = nlohmann::json::array();
        for (const auto& m : c.members) {
            nlohmann::json mj = {{"name", m.name}, {"offset", m.offset}, {"flags", m.flags}};
            if (m.typeRtti) mj["type"] = m.typeName;
            else mj["category"] = true;
            members.push_back(std::move(mj));
        }
        arr.push_back(std::move(cj));
    }
    return j;
}

}  // namespace dsloader::rtti
