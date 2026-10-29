#pragma once

#include <string_view>

enum class LogLevel {
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
    off,
};

[[nodiscard]]
constexpr std::string_view LogLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::trace : return "TRACE";
    case LogLevel::debug : return "DEBUG";
    case LogLevel::info  : return "INFO";
    case LogLevel::warn  : return "WARN";
    case LogLevel::error : return "ERROR";
    case LogLevel::fatal : return "FATAL";
    case LogLevel::off   : return "OFF";
    }

    return "UNKNOWN";
}

// 给定这条日志的级别和全局阈值，返回是否可以输出这条日志
[[nodiscard]]
constexpr bool isLogEnable(LogLevel messageLevel, LogLevel threshold) noexcept {
    return messageLevel != LogLevel::off && threshold != LogLevel::off && messageLevel >= threshold;
}