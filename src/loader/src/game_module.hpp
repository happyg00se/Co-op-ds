#pragma once
// PE module introspection of the running game exe: sections, build id, main window detection.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dsloader/plugin_api.h"
#include "sigscan.hpp"

namespace dsloader {

struct Section {
    std::string name;
    uintptr_t begin = 0;
    size_t size = 0;
    uint32_t characteristics = 0;
    bool executable() const { return (characteristics & 0x20000000u) != 0; }
    bool writable() const { return (characteristics & 0x80000000u) != 0; }
};

class GameModule {
public:
    /// Inspect the main module of this process. Returns false if PE headers look wrong.
    bool init(std::string* err);

    const DSGameModule& api() const { return api_; }
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(api_.base); }
    size_t size() const { return api_.size; }
    uint32_t buildId() const { return api_.build_id; }
    const std::string& exePath() const { return exePath_; }
    const std::vector<Section>& sections() const { return sections_; }
    const Section* section(const std::string& name) const;

    /// Region covering the whole image (for RIP resolution) and the executable sections only (for scanning).
    sig::Region imageRegion() const;
    std::vector<sig::Region> codeRegions() const;

    /// Top-level visible window owned by this process, or nullptr.
    static void* findMainWindow();

private:
    DSGameModule api_{};
    std::string exePath_;
    std::vector<Section> sections_;
};

/// ReadProcessMemory-based read that never faults.
bool safeRead(const void* src, void* dst, size_t size);

}  // namespace dsloader
