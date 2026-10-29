#include <cstring>

#include "minitest.hpp"
#include "rtti_scan.hpp"
#include "sigscan.hpp"

using namespace dsloader;

TEST(pattern_parse) {
    auto p = sig::parsePattern("48 8B 05 ?? ?? ?? ?? 48 85 c0");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), size_t(10));
    CHECK(p->mask[0]); CHECK(!p->mask[3]); CHECK_EQ(p->bytes[9], 0xC0);
    CHECK(!sig::parsePattern("ZZ 00").has_value());
    CHECK(!sig::parsePattern("?? ??").has_value());
    CHECK(!sig::parsePattern("").has_value());
}

TEST(find_and_rip_resolve) {
    // Fake module: base 0x140000000. At +0x100: "48 8B 05 <disp32> 48 85 C0" = mov rax,[rip+disp]; test rax,rax
    std::vector<uint8_t> img(0x1000, 0x90);
    const uintptr_t base = 0x140000000ULL;
    const size_t at = 0x100;
    const uint8_t code[] = {0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x85, 0xC0};
    std::memcpy(img.data() + at, code, sizeof(code));
    // target = at + 7 + disp ; want target = 0x800 -> disp = 0x800 - 0x107
    const int32_t disp = static_cast<int32_t>(0x800 - (at + 7));
    std::memcpy(img.data() + at + 3, &disp, 4);
    sig::Region r{img.data(), img.size(), base};

    sig::SigEntry e; e.name = "g_Thing"; e.pattern = "48 8B 05 ?? ?? ?? ?? 48 85 C0"; e.rip = sig::RipRel{3, 7};
    std::string err;
    auto addr = sig::resolve(r, e, &err);
    CHECK(addr.has_value());
    CHECK_EQ(*addr, base + 0x800);

    // Ambiguity is an error: add a second copy.
    std::memcpy(img.data() + 0x600, code, sizeof(code));
    CHECK(!sig::resolve(r, e, &err).has_value());
    CHECK(err.find("ambiguous") != std::string::npos);

    // Offset without rip.
    std::vector<uint8_t> img2(0x200, 0);
    img2[0x50] = 0xDE; img2[0x51] = 0xAD;
    sig::Region r2{img2.data(), img2.size(), base};
    sig::SigEntry e2; e2.pattern = "DE AD"; e2.offset = 2;
    auto a2 = sig::resolve(r2, e2, &err);
    CHECK(a2.has_value()); CHECK_EQ(*a2, base + 0x52);
}

TEST(load_signatures_from_json) {
    auto j = nlohmann::json::parse(R"({"signatures": {
        "A": {"pattern": "48 8B", "offset": 1, "rip": {"disp": 3, "len": 7}, "required": true, "note": "n"},
        "B": {"pattern": ""}
    }})");
    auto sigs = sig::loadSignatures(j);
    CHECK_EQ(sigs.size(), size_t(2));
    const auto& a = sigs[0].name == "A" ? sigs[0] : sigs[1];
    CHECK(a.required); CHECK(a.rip.has_value()); CHECK_EQ(a.rip->len, 7); CHECK_EQ(a.offset, 1);
}

TEST(fnv) {
    const char* s = "hello";
    CHECK_EQ(sig::fnv1a32(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s), 5)), 0x4f9f2cabu);
}

// Build a synthetic module with two RTTI class records following the default layout and scan it.
TEST(rtti_scan_synthetic) {
    rtti::Layout l;  // defaults
    const uintptr_t base = 0x140000000ULL;
    std::vector<uint8_t> img(0x4000, 0);
    // .rdata strings at 0x1000.., .data records at 0x2000..
    auto put = [&](size_t off, const void* p, size_t n) { std::memcpy(img.data() + off, p, n); };
    auto putStr = [&](size_t off, const char* s) { std::memcpy(img.data() + off, s, std::strlen(s) + 1); return base + off; };
    const uint64_t nEntity = putStr(0x1000, "Entity");
    const uint64_t nMover = putStr(0x1010, "Mover");
    const uint64_t nPos = putStr(0x1020, "Position");
    const uint64_t nFloat = putStr(0x1030, "float");
    const uint64_t nRef = putStr(0x1040, "RTTIRefObject");

    auto writeClass = [&](size_t off, uint64_t name, uint32_t size, uint8_t nBases, uint16_t nMembers, uint64_t baseTab, uint64_t memTab) {
        uint8_t kind = static_cast<uint8_t>(l.kindClass);
        put(off + l.kindOffset, &kind, 1);
        put(off + l.inheritanceCountOffset, &nBases, 1);
        put(off + l.memberCountOffset, &nMembers, 2);
        put(off + l.sizeOffset, &size, 4);
        put(off + l.nameOffset, &name, 8);
        put(off + l.inheritanceTableOffset, &baseTab, 8);
        put(off + l.memberTableOffset, &memTab, 8);
        return base + off;
    };
    // A primitive-kind record for "float" at 0x2F00 (kind 0, name pointer at +8).
    {
        uint8_t kind = static_cast<uint8_t>(l.kindPrimitive);
        put(0x2F00 + l.kindOffset, &kind, 1);
        put(0x2F00 + 8, &nFloat, 8);
    }
    const uint64_t refObj = writeClass(0x2000, nRef, 0x10, 0, 0, 0, 0);
    // Entity : RTTIRefObject, members: [category "Position"(type 0)], posX float @0x30
    size_t baseTabOff = 0x2200, memTabOff = 0x2300;
    put(baseTabOff + l.inhTypeOffset, &refObj, 8);
    uint32_t zero = 0; put(baseTabOff + l.inhOffsetOffset, &zero, 4);
    uint64_t nullp = 0; put(memTabOff + l.memberTypeOffset, &nullp, 8); put(memTabOff + l.memberNameOffset, &nPos, 8);
    uint64_t floatRtti = base + 0x2F00; uint16_t off30 = 0x30, flags = 0;
    put(memTabOff + l.memberEntrySize + l.memberTypeOffset, &floatRtti, 8);
    put(memTabOff + l.memberEntrySize + l.memberOffsetOffset, &off30, 2);
    put(memTabOff + l.memberEntrySize + l.memberFlagsOffset, &flags, 2);
    const uint64_t nX = putStr(0x1050, "PosX");
    put(memTabOff + l.memberEntrySize + l.memberNameOffset, &nX, 8);
    const uint64_t entity = writeClass(0x2100, nEntity, 0x200, 1, 2, base + baseTabOff, base + memTabOff);
    // Mover : Entity
    size_t baseTab2 = 0x2400;
    put(baseTab2 + l.inhTypeOffset, &entity, 8); put(baseTab2 + l.inhOffsetOffset, &zero, 4);
    writeClass(0x2500, nMover, 0x300, 1, 0, base + baseTab2, 0);
    // Junk record: class kind but name pointer into nowhere.
    { uint8_t kind = static_cast<uint8_t>(l.kindClass); put(0x2800 + l.kindOffset, &kind, 1); uint64_t bad = 0x7777; put(0x2800 + l.nameOffset, &bad, 8); }

    rtti::MemoryView mem{base, img.data(), img.size()};
    rtti::Range data{base + 0x2000, base + 0x3000};
    rtti::Range rdata{base + 0x1000, base + 0x2000};
    auto classes = rtti::scanClasses(mem, l, data, rdata);
    CHECK_EQ(classes.size(), size_t(3));
    rtti::resolveTypeNames(mem, l, classes, rdata);
    const rtti::ClassInfo* ent = nullptr;
    for (auto& c : classes) if (c.name == "Entity") ent = &c;
    CHECK(ent != nullptr);
    if (ent) {
        CHECK_EQ(ent->size, 0x200u);
        CHECK_EQ(ent->bases.size(), size_t(1)); CHECK_EQ(ent->bases[0].name, std::string("RTTIRefObject"));
        CHECK_EQ(ent->members.size(), size_t(2));
        CHECK_EQ(ent->members[0].name, std::string("Position")); CHECK_EQ(ent->members[0].typeRtti, uintptr_t(0));
        CHECK_EQ(ent->members[1].name, std::string("PosX")); CHECK_EQ(ent->members[1].offset, 0x30u);
        CHECK_EQ(ent->members[1].typeName, std::string("float"));
    }
    auto j = rtti::toJson(classes, l, 0xABCD, base);
    CHECK_EQ(j["class_count"].get<size_t>(), size_t(3));
    CHECK_EQ(j["classes"][1]["name"].get<std::string>(), std::string("Entity"));
}

int main() { return RUN_ALL_TESTS(); }
