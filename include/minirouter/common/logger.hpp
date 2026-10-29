#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace minirouter {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    NONE = 4
};

class Logger {
public:
    static Logger& instance() {
        static Logger instance_;
        return instance_;
    }

    void set_level(LogLevel level) {
        level_ = level;
    }

    LogLevel get_level() const {
        return level_;
    }

    void log(LogLevel level, const std::string& msg) {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#if defined(_WIN32)
        localtime_s(&tm_buf, &in_time_t);
#else
        localtime_r(&in_time_t, &tm_buf);
#endif

        std::cout << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                  << '.' << std::setfill('0') << std::setw(3) << ms.count() << "] ["
                  << level_to_string(level) << "] "
                  << msg << "\n";
    }

private:
    Logger() : level_(LogLevel::INFO) {}
    LogLevel level_;
    std::mutex mutex_;

    static const char* level_to_string(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default:              return "LOG  ";
        }
    }
};

#define LOG_DEBUG(msg) do { \
    if (minirouter::Logger::instance().get_level() <= minirouter::LogLevel::DEBUG) { \
        std::ostringstream _oss; _oss << msg; \
        minirouter::Logger::instance().log(minirouter::LogLevel::DEBUG, _oss.str()); \
    } \
} while(0)

#define LOG_INFO(msg) do { \
    if (minirouter::Logger::instance().get_level() <= minirouter::LogLevel::INFO) { \
        std::ostringstream _oss; _oss << msg; \
        minirouter::Logger::instance().log(minirouter::LogLevel::INFO, _oss.str()); \
    } \
} while(0)

#define LOG_WARN(msg) do { \
    if (minirouter::Logger::instance().get_level() <= minirouter::LogLevel::WARN) { \
        std::ostringstream _oss; _oss << msg; \
        minirouter::Logger::instance().log(minirouter::LogLevel::WARN, _oss.str()); \
    } \
} while(0)

#define LOG_ERROR(msg) do { \
    if (minirouter::Logger::instance().get_level() <= minirouter::LogLevel::ERROR) { \
        std::ostringstream _oss; _oss << msg; \
        minirouter::Logger::instance().log(minirouter::LogLevel::ERROR, _oss.str()); \
    } \
} while(0)

} // namespace minirouter
