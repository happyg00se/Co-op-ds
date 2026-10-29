#include "game_module.hpp"

#include <windows.h>

#include <cstring>

namespace dsloader {

bool safeRead(const void* src, void* dst, size_t size) {
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), src, dst, size, &got)) return false;
    return got == size;
}

bool GameModule::init(std::string* err) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) { if (err) *err = "GetModuleHandle(NULL) failed"; return false; }
    const auto base = reinterpret_cast<uintptr_t>(exe);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { if (err) *err = "bad DOS header"; return false; }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        if (err) *err = "not a PE32+ image";
        return false;
    }
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(exe, path, static_cast<DWORD>(std::size(path)));
    if (n) {
        int len = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        exePath_.resize(static_cast<size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(n), exePath_.data(), len, nullptr, nullptr);
    }

    api_.base = exe;
    api_.size = nt->OptionalHeader.SizeOfImage;
    api_.timestamp = nt->FileHeader.TimeDateStamp;
    api_.exe_path_utf8 = exePath_.c_str();
    struct { uint32_t ts, size, checksum; } key{nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage, nt->OptionalHeader.CheckSum};
    api_.build_id = sig::fnv1a32(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&key), sizeof(key)));

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    sections_.clear();
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        Section s;
        char name[9] = {0};
        std::memcpy(name, sec->Name, 8);
        s.name = name;
        s.begin = base + sec->VirtualAddress;
        s.size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
        s.characteristics = sec->Characteristics;
        sections_.push_back(s);
    }
    auto fill = [&](const char* nm, void*& b, size_t& sz) {
        if (const Section* s = section(nm)) { b = reinterpret_cast<void*>(s->begin); sz = s->size; }
    };
    fill(".text", api_.text_begin, api_.text_size);
    fill(".data", api_.data_begin, api_.data_size);
    fill(".rdata", api_.rdata_begin, api_.rdata_size);
    return true;
}

const Section* GameModule::section(const std::string& name) const {
    for (const auto& s : sections_) if (s.name == name) return &s;
    return nullptr;
}

sig::Region GameModule::imageRegion() const {
    return sig::Region{reinterpret_cast<const uint8_t*>(api_.base), api_.size, base()};
}

std::vector<sig::Region> GameModule::codeRegions() const {
    std::vector<sig::Region> out;
    for (const auto& s : sections_) {
        if (!s.executable()) continue;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(s.begin), &mbi, sizeof(mbi)) == 0) continue;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD)) continue;
        out.push_back(sig::Region{reinterpret_cast<const uint8_t*>(s.begin), s.size, s.begin});
    }
    return out;
}

namespace {
struct FindCtx { DWORD pid; HWND found; };
BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    wchar_t title[128] = {0};
    if (GetWindowTextW(hwnd, title, 127) == 0) return TRUE;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (rc.right - rc.left < 200 || rc.bottom - rc.top < 200) return TRUE;  // splash/console windows
    ctx->found = hwnd;
    return FALSE;
}
}  // namespace

void* GameModule::findMainWindow() {
    FindCtx ctx{GetCurrentProcessId(), nullptr};
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

}  // namespace dsloader
