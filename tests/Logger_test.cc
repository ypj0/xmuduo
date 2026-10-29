#include "Logger.h"
#include "LogBuffer.h"
#include "TestSupport.h"
#include "Timestamp.h"

#include <array>
#include <atomic>
#include <barrier>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr std::array levels{LogLevel::trace, LogLevel::debug, LogLevel::info,
    LogLevel::warn, LogLevel::error, LogLevel::fatal, LogLevel::off};
}

int main() {
    return runTests([] {
        CHECK(Logger::level() == LogLevel::info);
        for (std::size_t threshold = 0; threshold < levels.size(); ++threshold) {
            Logger::setLevel(levels[threshold]);
            CHECK(Logger::level() == levels[threshold]);
            for (std::size_t message = 0; message < levels.size(); ++message) {
                const bool expected = threshold < 6 && message < 6 && message >= threshold;
                CHECK(Logger::enable(levels[message]) == expected);
            }
        }

        // Readers may observe any concurrently stored level, but never an
        // invalid value. Do not assume immediate visibility or thread order.
        std::barrier start{4};
        std::atomic<bool> valid{true};
        std::vector<std::jthread> workers;
        for (std::size_t worker = 0; worker < 4; ++worker) {
            workers.emplace_back([&, worker] {
                start.arrive_and_wait();
                for (std::size_t i = 0; i < 10000; ++i) {
                    Logger::setLevel(levels[(i + worker) % levels.size()]);
                    const auto observed = Logger::level();
                    if (observed < LogLevel::trace || observed > LogLevel::off ||
                        Logger::enable(LogLevel::off)) {
                        valid.store(false, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (auto& worker : workers) { worker.join(); }
        CHECK(valid.load(std::memory_order_relaxed));
        Logger::setLevel(LogLevel::info);
        CHECK(Logger::level() == LogLevel::info);

        struct Capture {
            std::vector<std::string> messages;
            int flushes = 0;
        };
        auto capture = std::make_shared<Capture>();
        Logger::setOutput(
            [capture](std::string_view message) { capture->messages.emplace_back(message); },
            [capture] { ++capture->flushes; });

        CHECK(Logger::log(LogLevel::debug, "filtered"));
        CHECK(capture->messages.empty());
        const auto before = Timestamp::now().toUtcString();
        const auto callLine = __LINE__ + 1;
        CHECK(Logger::log(LogLevel::info, "hello"));
        const auto after = Timestamp::now().toUtcString();
        CHECK(capture->messages.size() == 1);
        const auto& message = capture->messages.back();
        CHECK(message.starts_with("["));
        const auto time = message.substr(1, before.size());
        CHECK(time >= before && time <= after);
        CHECK(message.substr(1 + before.size()) ==
              "] [INFO] hello - " + std::string{__FILE__} + ":" +
                  std::to_string(callLine) + "\n");
        CHECK(capture->flushes == 0);
        Logger::flush();
        CHECK(capture->flushes == 1);

        // Use a fixed source location so footer length is identical at each call.
        const auto location = std::source_location::current();
        CHECK(Logger::log(LogLevel::info, "", location));
        const auto overhead = capture->messages.back().size();
        CHECK(overhead < LogBuffer<>::capacity());
        const std::string exact(LogBuffer<>::capacity() - overhead, 'x');
        CHECK(Logger::log(LogLevel::info, exact, location));
        CHECK(capture->messages.back().size() == LogBuffer<>::capacity());
        const auto count = capture->messages.size();
        CHECK(!Logger::log(LogLevel::info, exact + 'x', location));
        CHECK(capture->messages.size() == count);
        CHECK(Logger::log(LogLevel::info, "after overflow", location));
        CHECK(Logger::log(LogLevel::info, std::string_view{"a\0b", 3}, location));
        CHECK(capture->messages.back().find(std::string{"a\0b", 3}) != std::string::npos);

        Logger::setLevel(LogLevel::off);
        const auto countBeforeOff = capture->messages.size();
        CHECK(Logger::log(LogLevel::error, "filtered"));
        CHECK(capture->messages.size() == countBeforeOff);
        Logger::setLevel(LogLevel::info);

        expectThrows<std::invalid_argument>([] { Logger::setOutput({}, [] {}); });
        CHECK(Logger::log(LogLevel::warn, "still configured"));
        CHECK(capture->messages.size() == countBeforeOff + 1);

        const auto formattedLocation = std::source_location::current();
        int fd = 42;
        CHECK(Logger::logf(LogLevel::info, formattedLocation,
                           "fd={}, elapsed={:.2f}, hex={:04x}", fd, 1.25, 15));
        CHECK(capture->messages.back().ends_with(
            "] [INFO] fd=42, elapsed=1.25, hex=000f - " +
            std::string{formattedLocation.file_name()} + ":" +
            std::to_string(formattedLocation.line()) + "\n"));
        CHECK(Logger::logf(LogLevel::info, formattedLocation, "no arguments"));
        const auto formattedCount = capture->messages.size();
        CHECK(!Logger::logf(LogLevel::info, formattedLocation,
                            "{}", std::string(LogBuffer<>::capacity() + 1, 'x')));
        // The body fits, but leaves no space for the log header/footer.
        CHECK(!Logger::logf(LogLevel::info, formattedLocation,
                            "{}", std::string(LogBuffer<>::capacity(), 'x')));
        CHECK(capture->messages.size() == formattedCount);
        expectThrows<std::format_error>([&] {
            (void)Logger::logf(LogLevel::info, formattedLocation, "{:{}}", 42, -1);
        });
        CHECK(capture->messages.size() == formattedCount);
        // Invalid runtime width is never formatted for a disabled level.
        CHECK(Logger::logf(LogLevel::debug, formattedLocation, "{:{}}", 42, -1));
        CHECK(capture->messages.size() == formattedCount);

        Logger::setLevel(LogLevel::info);
        int evaluations = 0;
        LOG_DEBUG("value={}", ++evaluations);
        CHECK(evaluations == 0);
        CHECK(capture->messages.size() == formattedCount);

        const auto macroLine = __LINE__ + 1;
        LOG_INFO("macro value={}", 7);
        CHECK(capture->messages.back().ends_with(
            "] [INFO] macro value=7 - " + std::string{__FILE__} + ":" +
            std::to_string(macroLine) + "\n"));
        const auto macroCount = capture->messages.size();
        if (false)
            LOG_WARN("unreachable");
        else
            LOG_ERROR("else works");
        CHECK(capture->messages.size() == macroCount + 1);
        CHECK(capture->messages.back().find("] [ERROR] else works - ") != std::string::npos);

        Logger::setLevel(LogLevel::trace);
        LOG_TRACE("trace");
        LOG_DEBUG("debug");
        LOG_WARN("warn");
        CHECK(capture->messages[capture->messages.size() - 3].find("] [TRACE] trace - ") != std::string::npos);
        CHECK(capture->messages[capture->messages.size() - 2].find("] [DEBUG] debug - ") != std::string::npos);
        CHECK(capture->messages.back().find("] [WARN] warn - ") != std::string::npos);
        Logger::setLevel(LogLevel::info);
        const auto beforeOverflowMacro = capture->messages.size();
        expectThrows<std::length_error>([] {
            LOG_INFO("{}", std::string(LogBuffer<>::capacity(), 'x'));
        });
        CHECK(capture->messages.size() == beforeOverflowMacro);
        expectThrows<std::format_error>([] { LOG_INFO("{:{}}", 42, -1); });
        CHECK(capture->messages.size() == beforeOverflowMacro);

        Logger::setOutput([](std::string_view) { throw std::runtime_error("output failed"); }, [] {});
        expectThrows<std::runtime_error>([] { (void)Logger::log(LogLevel::error, "error"); });
        Logger::setOutput([](std::string_view) {}, [] {});
        CHECK(Logger::log(LogLevel::info, "lock released"));
    });
}
