#include "pe.hpp"

#include <cstring>
#include <fstream>
#include <iterator>

namespace dsinst::pe {
namespace {
template <class T>
bool rd(std::span<const uint8_t> b, size_t off, T& out) {
    if (off > b.size() || b.size() - off < sizeof(T)) return false;
    std::memcpy(&out, b.data() + off, sizeof(T));
    return true;
}
}  // namespace

std::optional<std::vector<std::string>> exportNames(std::span<const uint8_t> b) {
    uint16_t mz = 0;
    uint32_t lfanew = 0, sig = 0;
    if (!rd(b, 0, mz) || mz != 0x5A4D || !rd(b, 0x3C, lfanew) || !rd(b, lfanew, sig) || sig != 0x00004550) return std::nullopt;
    const size_t fh = lfanew + 4;
    uint16_t nsec = 0, optSize = 0, magic = 0;
    if (!rd(b, fh + 2, nsec) || !rd(b, fh + 16, optSize)) return std::nullopt;
    const size_t opt = fh + 20;
    if (!rd(b, opt, magic)) return std::nullopt;
    const size_t ddOff = magic == 0x20B ? 112 : magic == 0x10B ? 96 : 0;
    if (!ddOff || ddOff + 8 > optSize) return std::nullopt;
    uint32_t expRva = 0, expSize = 0;
    if (!rd(b, opt + ddOff, expRva) || !rd(b, opt + ddOff + 4, expSize)) return std::nullopt;
    std::vector<std::string> names;
    if (expRva == 0) return names;

    const size_t secTab = opt + optSize;
    auto rvaToOff = [&](uint32_t rva) -> std::optional<size_t> {
        for (uint16_t i = 0; i < nsec; ++i) {
            const size_t s = secTab + static_cast<size_t>(i) * 40;
            uint32_t vsize = 0, va = 0, rawSize = 0, rawPtr = 0;
            if (!rd(b, s + 8, vsize) || !rd(b, s + 12, va) || !rd(b, s + 16, rawSize) || !rd(b, s + 20, rawPtr)) return std::nullopt;
            const uint32_t span = vsize > rawSize ? vsize : rawSize;
            if (rva >= va && rva < va + span) {
                const size_t off = static_cast<size_t>(rawPtr) + (rva - va);
                if (off >= b.size()) return std::nullopt;
                return off;
            }
        }
        return std::nullopt;
    };
    auto ed = rvaToOff(expRva);
    if (!ed) return std::nullopt;
    uint32_t numNames = 0, namesRva = 0;
    if (!rd(b, *ed + 24, numNames) || !rd(b, *ed + 32, namesRva)) return std::nullopt;
    if (numNames > 65536) return std::nullopt;
    auto namesOff = rvaToOff(namesRva);
    if (numNames && !namesOff) return std::nullopt;
    for (uint32_t i = 0; i < numNames; ++i) {
        uint32_t nameRva = 0;
        if (!rd(b, *namesOff + static_cast<size_t>(i) * 4, nameRva)) return std::nullopt;
        auto no = rvaToOff(nameRva);
        if (!no) return std::nullopt;
        std::string n;
        for (size_t k = *no; k < b.size() && b[k] != 0 && n.size() < 512; ++k) n.push_back(static_cast<char>(b[k]));
        names.push_back(std::move(n));
    }
    return names;
}

std::optional<std::vector<std::string>> exportNamesOfFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return exportNames(data);
}

bool isDsLoader(std::span<const uint8_t> image) {
    auto names = exportNames(image);
    if (!names) return false;
    for (const auto& n : *names) if (n == "DSLoader_GetVersion") return true;
    return false;
}

bool isDsLoader(const std::filesystem::path& file) {
    auto names = exportNamesOfFile(file);
    if (!names) return false;
    for (const auto& n : *names) if (n == "DSLoader_GetVersion") return true;
    return false;
}

}  // namespace dsinst::pe
