#pragma once

#include "noncopyable.h"

#include <cstdlib>
#include <mutex>
#include <string_view>

namespace xmuduo {

enum class LogLevel {
    Info,
    Error,
    Fatal,
    Debug,
};

class Logger : private noncopyable {
public:
    static Logger& instance();

    // 等级属于当前消息，不作为单例的共享状态保存。
    void log(LogLevel level, std::string_view msg);
    void logf(LogLevel level, const char* format, ...);

private:
    Logger() = default;
    std::mutex mutex_;
};

} // namespace xmuduo

// LOG_INFO("%s %d", name, count)，也支持 LOG_INFO("hello")。
#define LOG_INFO(logmsgFormat, ...) \
    do { \
        ::xmuduo::Logger::instance().logf( \
            ::xmuduo::LogLevel::Info, logmsgFormat __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define LOG_ERROR(logmsgFormat, ...) \
    do { \
        ::xmuduo::Logger::instance().logf( \
            ::xmuduo::LogLevel::Error, logmsgFormat __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define LOG_FATAL(logmsgFormat, ...) \
    do { \
        ::xmuduo::Logger::instance().logf( \
            ::xmuduo::LogLevel::Fatal, logmsgFormat __VA_OPT__(,) __VA_ARGS__); \
        ::std::exit(EXIT_FAILURE); \
    } while (0)

#if defined(XMUDUO_ENABLE_DEBUG_LOG) && XMUDUO_ENABLE_DEBUG_LOG
#define LOG_DEBUG(logmsgFormat, ...) \
    do { \
        ::xmuduo::Logger::instance().logf( \
            ::xmuduo::LogLevel::Debug, logmsgFormat __VA_OPT__(,) __VA_ARGS__); \
    } while (0)
#else
#define LOG_DEBUG(...) do {} while (0)
#endif
