#include "Logger.h"
#include "LogLevel.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <utility>
#include "LogBuffer.h"
#include "Timestamp.h"


namespace {
    std::mutex outputMutex;
    std::atomic<LogLevel> currentLevel{LogLevel::info};
    Logger::OutputFunc outputFunc = [](std::string_view message) {
        if(message.empty()) {
            return;
        }
        const auto written = 
            std::fwrite(message.data(), 1, message.size(), stdout);

        if(written != message.size()) {
            throw std::runtime_error("Failed to write log");
        }
    };

    Logger::FlushFunc flushFunc = [] {
        if(std::fflush(stdout) != 0) {
            throw std::runtime_error("Failed to flush log");
        }
    };
}

void Logger::setOutput(OutputFunc output, FlushFunc flush) {
    if(!output || !flush) {
        throw std::invalid_argument("Logger callbacks must not be empty");
    }
    {
        std::lock_guard lock{outputMutex};
        outputFunc.swap(output);
        flushFunc.swap(flush);
    }
}


void Logger::write(std::string_view message) {
    std::lock_guard lock{outputMutex};
    outputFunc(message);
}

void Logger::flush() {
    std::lock_guard lock{outputMutex};
    flushFunc();
}
void Logger::setLevel(LogLevel level) noexcept {
    currentLevel.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() noexcept {
    return currentLevel.load(std::memory_order_relaxed);
}

bool Logger::enable(LogLevel messageLevel) noexcept {
    return isLogEnable(messageLevel, level());
}

bool Logger::log(LogLevel level, std::string_view message, std::source_location location) {
    if(!enable(level)) {
        return true;
    }
    LogBuffer<> buffer;
    // [UTC时间] [等级] 正文 - 源文件：行号
    const bool success = buffer.appendFormat(
        "[{}] [{}] {} - {}:{}\n",
        Timestamp::now().toUtcString(),
        LogLevelName(level),
        message,
        location.file_name(),
        location.line());

    if(!success) {
        return false;
    }

    write(buffer.view());
    return true;
}
