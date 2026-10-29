#include "dscommon/log.hpp"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>

namespace dscommon::log {
namespace {

class RingSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit RingSink(size_t cap) : cap_(cap) {}
    std::vector<std::string> lines(size_t max) {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::string> out;
        const size_t n = std::min(max, ring_.size());
        out.reserve(n);
        for (size_t i = ring_.size() - n; i < ring_.size(); ++i) out.push_back(ring_[i]);
        return out;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        std::string line(formatted.data(), formatted.size());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        ring_.push_back(std::move(line));
        while (ring_.size() > cap_) ring_.pop_front();
    }
    void flush_() override {}

private:
    size_t cap_;
    std::deque<std::string> ring_;
};

std::shared_ptr<spdlog::logger> g_logger;
std::shared_ptr<RingSink> g_ring;
std::mutex g_mutex;

}  // namespace

std::shared_ptr<spdlog::logger> init(const Options& opts) {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::vector<spdlog::sink_ptr> sinks;
    try {
        if (!opts.file.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(opts.file.parent_path(), ec);
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                opts.file.string(), opts.maxFileBytes, opts.maxFiles));
        }
    } catch (const std::exception&) {
        // Fall through: logging must never take the game down.
    }
    if (opts.console) sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    g_ring = std::make_shared<RingSink>(opts.ringCapacity);
    sinks.push_back(g_ring);

    g_logger = std::make_shared<spdlog::logger>(opts.name, sinks.begin(), sinks.end());
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [t%t] %v");
    g_logger->set_level(opts.level);
    g_logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(g_logger);
    return g_logger;
}

std::shared_ptr<spdlog::logger> get() {
    if (!g_logger) {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!g_logger) {
            g_ring = std::make_shared<RingSink>(500);
            g_logger = std::make_shared<spdlog::logger>("dscoop", g_ring);
            g_logger->set_level(spdlog::level::trace);
        }
    }
    return g_logger;
}

void shutdown() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_logger) g_logger->flush();
    spdlog::shutdown();
    g_logger.reset();
    g_ring.reset();
}

std::vector<std::string> recentLines(size_t max) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_ring ? g_ring->lines(max) : std::vector<std::string>{};
}

spdlog::level::level_enum parseLevel(const std::string& s) {
    if (s == "trace") return spdlog::level::trace;
    if (s == "debug") return spdlog::level::debug;
    if (s == "warn" || s == "warning") return spdlog::level::warn;
    if (s == "error") return spdlog::level::err;
    if (s == "off") return spdlog::level::off;
    return spdlog::level::info;
}

}  // namespace dscommon::log
