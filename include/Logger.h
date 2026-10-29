#pragma once 

#include "LogLevel.h"
#include <functional>
#include <string_view>
#include <source_location>
#include "LogBuffer.h"
#include <format>
#include <utility>
#include <stdexcept>
#include <stdexcept>

class Logger {
public:
    using OutputFunc = std::function<void(std::string_view)>;
    using FlushFunc = std::function<void()>;

    static void setOutput(OutputFunc output, FlushFunc flush);

    static void write(std::string_view message);
    static void flush();

    Logger() = delete;
    static void setLevel(LogLevel level) noexcept;

    [[nodiscard]]
    static LogLevel level() noexcept;

    [[nodiscard]]
    static bool enable(LogLevel messageLevel) noexcept;

    [[nodiscard]]
    static bool log(
        LogLevel level,
        std::string_view message,
        std::source_location location = std::source_location::current());

    template<typename... Args>
    [[nodiscard]]
    static bool logf(
        LogLevel level,
        std::source_location location,
        std::format_string<Args...> format,
        Args&&... args) {
        if(!enable(level)) {
            return true;
        }

        LogBuffer<> body;
        if(!body.appendFormat(format, std::forward<Args>(args)...)) {
            return false;
        }

        return log(level, body.view(), location);
    }
};

// The outer check keeps formatting arguments unevaluated for disabled levels.
#define XMUDUO_DETAIL_LOG(level, ...)                                      \
    do {                                                                    \
        if (::Logger::enable(level)) {                                      \
            if (!::Logger::logf(                                            \
                    level,                                                  \
                    ::std::source_location::current(),                     \
                    __VA_ARGS__)) {                                         \
                throw ::std::length_error("Log message exceeds capacity"); \
            }                                                               \
        }                                                                   \
    } while (false)

#define LOG_TRACE(...) XMUDUO_DETAIL_LOG(::LogLevel::trace, __VA_ARGS__)
#define LOG_DEBUG(...) XMUDUO_DETAIL_LOG(::LogLevel::debug, __VA_ARGS__)
#define LOG_INFO(...)  XMUDUO_DETAIL_LOG(::LogLevel::info, __VA_ARGS__)
#define LOG_WARN(...)  XMUDUO_DETAIL_LOG(::LogLevel::warn, __VA_ARGS__)
#define LOG_ERROR(...) XMUDUO_DETAIL_LOG(::LogLevel::error, __VA_ARGS__)
