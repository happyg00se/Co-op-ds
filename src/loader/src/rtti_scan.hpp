#pragma once
// Decima RTTI discovery. Decima has its own reflection system (not MSVC RTTI): every class
// has a static RTTI record with name, size, member table and base-class table. We scan the
// module's data sections for records matching a data-driven layout (offsets.json "rtti.layout")
// and dump them to JSON (docs/rtti-dump-format.md). Layout offsets differ between Decima
// versions, so nothing here is hardcoded — recon step 6 in docs/00-recon-guide.md verifies them.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dsloader::rtti {

/// Byte offsets inside the RTTI structures. -1 = unknown / not present.
struct Layout {
    // RTTI header (common to all kinds)
    int kindOffset = 2;          // u8 kind
    int kindClass = 4;           // value of `kind` for classes
    int kindEnum = 3;
    int kindPrimitive = 0;
    int kindMax = 7;             // reject kind >= kindMax
    // RTTIClass
    int inheritanceCountOffset = 7;   // u8
    int memberCountOffset = 8;        // u16
    int sizeOffset = 0x0C;            // u32 instance size
    int nameOffset = 0x38;            // const char*
    int prevOffset = 0x40;            // RTTI* (linked list, may be absent -> -1)
    int nextOffset = 0x48;
    int inheritanceTableOffset = 0x50;
    int memberTableOffset = 0x58;
    int headerSize = 0x80;            // minimal readable size of a class record
    // Inheritance entry
    int inhEntrySize = 0x10;
    int inhTypeOffset = 0;            // RTTI*
    int inhOffsetOffset = 8;          // u32 offset of base inside derived
    // Member entry
    int memberEntrySize = 0x30;
    int memberTypeOffset = 0;         // RTTI*
    int memberOffsetOffset = 8;       // u16
    int memberFlagsOffset = 0x0A;     // u16
    int memberNameOffset = 0x10;      // const char*
    // Heuristic limits
    uint32_t maxMembers = 2048;
    uint32_t maxBases = 32;
    uint32_t maxSize = 64u * 1024u * 1024u;
    bool verified = false;            // set true in offsets.json once recon confirmed the layout

    static Layout fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
};

/// Read-only view of the module image in memory (or a synthetic buffer in tests).
struct MemoryView {
    uintptr_t base = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
    bool contains(uintptr_t addr, size_t len = 1) const { return addr >= base && len <= size && addr - base <= size - len; }
    template <class T>
    std::optional<T> read(uintptr_t addr) const {
        if (!contains(addr, sizeof(T))) return std::nullopt;
        T v; std::memcpy(&v, data + (addr - base), sizeof(T)); return v;
    }
    std::optional<std::string> readCString(uintptr_t addr, size_t maxLen = 256) const;
};

struct Range { uintptr_t begin = 0, end = 0; bool contains(uintptr_t a) const { return a >= begin && a < end; } };

struct MemberInfo { std::string name; std::string typeName; uintptr_t typeRtti = 0; uint32_t offset = 0; uint16_t flags = 0; };
struct BaseInfo { std::string name; uintptr_t typeRtti = 0; uint32_t offset = 0; };
struct ClassInfo {
    uintptr_t address = 0;
    std::string name;
    uint32_t size = 0;
    uint8_t kind = 0;
    std::vector<BaseInfo> bases;
    std::vector<MemberInfo> members;
};

/// Name/kind of any RTTI record (class, enum, primitive...) — enough to label member types.
struct TypeBrief { std::string name; uint8_t kind = 0; };

/// Is there a plausible RTTI class record at `addr`?
std::optional<ClassInfo> tryReadClass(const MemoryView& mem, const Layout& l, uintptr_t addr, const Range& dataRange, const Range& rdataRange);

/// Scan `dataRange` (pointer-aligned) for class records. `rdataRange` is where name strings live
/// (may equal dataRange when unknown). Returns records sorted by address.
std::vector<ClassInfo> scanClasses(const MemoryView& mem, const Layout& l, const Range& dataRange, const Range& rdataRange,
                                   std::function<void(size_t found)> progress = nullptr);

/// Fill member type names by looking at the referenced RTTI records.
void resolveTypeNames(const MemoryView& mem, const Layout& l, std::vector<ClassInfo>& classes, const Range& rdataRange);

/// JSON dump: { "format": 1, "game_build": ..., "layout": {...}, "classes": [ ... ] }
nlohmann::json toJson(const std::vector<ClassInfo>& classes, const Layout& l, uint32_t gameBuild, uintptr_t moduleBase);

}  // namespace dsloader::rtti
