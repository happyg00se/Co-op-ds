#include "crash_handler.hpp"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

#include "dscommon/log.hpp"
#include "dscommon/paths.hpp"

namespace dsloader {
namespace {
std::string g_reportsDir;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;

LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    char path[MAX_PATH * 2];
    std::snprintf(path, sizeof(path), "%s\\crash_%s.dmp", g_reportsDir.c_str(), dscommon::paths::timestampForFiles().c_str());
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo),
                          ep ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(f);
    }
    DSLOG_ERROR("CRASH: exception {:#x} at {} — minidump {}", code, addr, path);
    dscommon::log::get()->flush();
    return g_previous ? g_previous(ep) : EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void installCrashHandler(const std::string& reportsDir) {
    g_reportsDir = reportsDir;
    g_previous = SetUnhandledExceptionFilter(filter);
}

}  // namespace dsloader
