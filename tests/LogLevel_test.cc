#include "LogLevel.h"
#include "TestSupport.h"

#include <array>
#include <string_view>

namespace {
constexpr std::array levels{LogLevel::trace, LogLevel::debug, LogLevel::info,
    LogLevel::warn, LogLevel::error, LogLevel::fatal, LogLevel::off};
constexpr std::array<std::string_view, 7> names{
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"};

constexpr bool verifyLevels() {
    for (std::size_t message = 0; message < levels.size(); ++message) {
        if (LogLevelName(levels[message]) != names[message]) { return false; }
        for (std::size_t threshold = 0; threshold < levels.size(); ++threshold) {
            const bool expected = message < 6 && threshold < 6 && message >= threshold;
            if (isLogEnable(levels[message], levels[threshold]) != expected) { return false; }
        }
    }
    return true;
}
static_assert(verifyLevels());
static_assert(LogLevelName(static_cast<LogLevel>(-1)) == "UNKNOWN");
static_assert(LogLevelName(static_cast<LogLevel>(100)) == "UNKNOWN");
} // namespace

int main() {
    return runTests([] {
        CHECK(verifyLevels());
        CHECK(LogLevelName(static_cast<LogLevel>(100)) == "UNKNOWN");
    });
}
