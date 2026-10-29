#include "Logger.h"
#include "LogLevel.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string_view>
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
    if (!appendPrefix(buffer, level) ||
        !buffer.append(message) ||
        !appendSuffix(buffer, location)) {
        return false;
    }
    write(buffer.view());
    return true;
}

bool Logger::appendPrefix(LogBuffer<>& buffer, LogLevel level) {
    return buffer.appendFormat(
        "[{}] [{}] ",
        Timestamp::now().toUtcString(),
        LogLevelName(level));
}

bool Logger::appendSuffix(LogBuffer<>& buffer, std::source_location location) {
    return buffer.appendFormat(
        " - {}:{}\n",
        location.file_name(),
        location.line());
}
