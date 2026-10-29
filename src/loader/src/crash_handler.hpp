#pragma once
#include <string>

namespace dsloader {
/// Installs an unhandled-exception filter that writes dscoop/reports/crash_<ts>.dmp + a log line, then
/// chains to the previous filter so the game's own handler still runs.
void installCrashHandler(const std::string& reportsDir);
}  // namespace dsloader
