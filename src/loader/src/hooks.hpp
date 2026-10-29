#pragma once
// MinHook wrapper with named bookkeeping so hooks show up in logs/reports and get removed on unload.
#include <map>
#include <mutex>
#include <string>

namespace dsloader {

class Hooks {
public:
    static Hooks& instance();
    bool init(std::string* err);
    void shutdown();

    /// Returns MH_STATUS (0 = MH_OK).
    int create(void* target, void* detour, void** original, const std::string& name = "");
    int enable(void* target);
    int disable(void* target);
    int remove(void* target);
    static const char* statusText(int status);

    std::map<void*, std::string> list() const;

private:
    mutable std::mutex mutex_;
    bool initialized_ = false;
    std::map<void*, std::string> names_;
};

}  // namespace dsloader
