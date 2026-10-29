#pragma once
// Thin wrapper over spdlog: one rotating file per component (dsloader.log, dscoop.log, ...)
// plus an in-memory ring consumed by the in-game console and the crash/report collector.
#include <spdlog/spdlog.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dscommon::log {

struct Options {
    std::filesystem::path file;          // e.g. <root>/logs/dsloader.log
    std::string name = "dscoop";
    spdlog::level::level_enum level = spdlog::level::info;
    size_t maxFileBytes = 5 * 1024 * 1024;
    size_t maxFiles = 3;
    bool console = false;                // stdout sink (tools / test client)
    size_t ringCapacity = 2000;          // lines kept for the overlay console
};

/// Initialise the process-wide default logger. Safe to call once per DLL/exe.
std::shared_ptr<spdlog::logger> init(const Options& opts);
std::shared_ptr<spdlog::logger> get();
void shutdown();

/// Recent lines for the in-game console and reports.
std::vector<std::string> recentLines(size_t max = 200);

spdlog::level::level_enum parseLevel(const std::string& s);

}  // namespace dscommon::log

#define DSLOG_TRACE(...) SPDLOG_LOGGER_TRACE(::dscommon::log::get(), __VA_ARGS__)
#define DSLOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::dscommon::log::get(), __VA_ARGS__)
#define DSLOG_INFO(...)  SPDLOG_LOGGER_INFO(::dscommon::log::get(), __VA_ARGS__)
#define DSLOG_WARN(...)  SPDLOG_LOGGER_WARN(::dscommon::log::get(), __VA_ARGS__)
#define DSLOG_ERROR(...) SPDLOG_LOGGER_ERROR(::dscommon::log::get(), __VA_ARGS__)
