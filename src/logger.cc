#include "logger.h"
#include "timestamp.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <vector>

namespace xmuduo {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, std::string_view msg) {
    const char* label = "UNKNOWN";
    switch (level) {
    case LogLevel::Info:
        label = "INFO";
        break;
    case LogLevel::Error:
        label = "ERROR";
        break;
    case LogLevel::Fatal:
        label = "FATAL";
        break;
    case LogLevel::Debug:
        label = "DEBUG";
        break;
    }

    // 锁住整条日志，避免不同线程的等级和正文交错。
    const std::lock_guard<std::mutex> lock(mutex_);
    std::cout << '[' << Timestamp::now().toString() << "] ["
              << label << "] " << msg << '\n';
    if (level == LogLevel::Fatal) {
        std::cout.flush();
    }
}

void Logger::logf(LogLevel level, const char* format, ...) {
    if (format == nullptr) {
        log(level, "[format error: null format]");
        return;
    }

    // 先尝试栈缓冲区，较长消息再按所需长度分配，避免静默截断。
    char buffer[1024];
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length < 0) {
        va_end(copy);
        log(level, "[format error]");
        return;
    }
    
    if (static_cast<std::size_t>(length) < sizeof(buffer)) {
        va_end(copy);
        log(level, std::string_view(buffer, static_cast<std::size_t>(length)));
        return;
    }

    std::vector<char> expanded;
    try {
        expanded.resize(static_cast<std::size_t>(length) + 1);
    } catch (...) {
        va_end(copy);
        throw;
    }
    const int written = std::vsnprintf(expanded.data(), expanded.size(), format, copy);
    va_end(copy);
    if (written < 0 || static_cast<std::size_t>(written) >= expanded.size()) {
        log(level, "[format error]");
        return;
    }
    log(level, std::string_view(expanded.data(), static_cast<std::size_t>(written)));
}

} // namespace xmuduo
