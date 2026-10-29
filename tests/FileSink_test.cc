#include "FileSink.h"
#include "Logger.h"
#include "TestSupport.h"

#include <chrono>
#include <atomic>
#include <barrier>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <thread>
#include <vector>

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("xmuduo-file-sink-" + std::to_string(nonce));
        std::filesystem::create_directory(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::string readAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { throw std::runtime_error("could not read test log file"); }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void restoreStdoutOutput() {
    Logger::setOutput(
        [](std::string_view message) {
            if (!message.empty() &&
                std::fwrite(message.data(), 1, message.size(), stdout) != message.size()) {
                throw std::runtime_error("stdout write failed");
            }
        },
        [] {
            if (std::fflush(stdout) != 0) { throw std::runtime_error("stdout flush failed"); }
        });
}
static_assert(!std::is_copy_constructible_v<FileSink>);
static_assert(!std::is_copy_assignable_v<FileSink>);
} // namespace

int main() {
    return runTests([] {
        TemporaryDirectory temporary;
        const auto path = temporary.path() / "direct.log";
        using namespace std::string_view_literals;
        const auto binaryPart = "with-zero:\0:end\n"sv;
        const auto expected = std::string{"first\n"} + std::string{binaryPart};
        {
            FileSink sink{path};
            sink.append("first\n");
            sink.append(binaryPart);
            sink.append("");
            sink.flush();
            CHECK(readAll(path) == expected);
        }
        {
            FileSink sink{path}; // Existing files are opened in append mode.
            sink.append("second\n");
            sink.flush();
        }
        CHECK(readAll(path) == expected + "second\n");
        {
            FileSink sink{path};
            sink.append("closed\n"); // RAII close also flushes stdio buffering.
        }
        CHECK(readAll(path) == expected + "second\nclosed\n");

        try {
            FileSink existing{path, FileSink::OpenMode::createExclusive};
            CHECK(false);
        } catch (const std::system_error& error) {
            CHECK(error.code() == std::errc::file_exists);
        }
        CHECK(readAll(path) == expected + "second\nclosed\n");
        const auto exclusivePath = temporary.path() / "exclusive.log";
        {
            FileSink exclusive{exclusivePath, FileSink::OpenMode::createExclusive};
            exclusive.append("exclusive\n");
            exclusive.flush();
            CHECK(readAll(exclusivePath) == "exclusive\n");
        }
        {
            FileSink append{exclusivePath};
            append.append("appended\n");
            append.flush();
        }
        CHECK(readAll(exclusivePath) == "exclusive\nappended\n");
        expectThrows<std::invalid_argument>([&] {
            FileSink invalid{path, static_cast<FileSink::OpenMode>(100)};
        });
        expectThrows<std::invalid_argument>([&] {
            FileSink invalid{std::filesystem::path{path.string() + std::string{"\0suffix", 7}}};
        });

        // Atomic exclusive creation: exactly one contender wins the same name.
        const auto contestedPath = temporary.path() / "contested.log";
        std::barrier start{4};
        std::atomic<int> winners{0};
        std::atomic<int> conflicts{0};
        std::atomic<int> unexpected{0};
        std::vector<std::jthread> contenders;
        for (int i = 0; i < 4; ++i) {
            contenders.emplace_back([&] {
                start.arrive_and_wait();
                try {
                    FileSink file{contestedPath, FileSink::OpenMode::createExclusive};
                    file.append("winner\n");
                    file.flush();
                    ++winners;
                } catch (const std::system_error& error) {
                    if (error.code() == std::errc::file_exists) { ++conflicts; }
                    else { ++unexpected; }
                } catch (...) {
                    ++unexpected;
                }
            });
        }
        for (auto& contender : contenders) { contender.join(); }
        CHECK(winners == 1);
        CHECK(conflicts == 3);
        CHECK(unexpected == 0);
        CHECK(readAll(contestedPath) == "winner\n");

#ifdef __linux__
        // Linux's full device accepts opens but rejects actual writes.
        if (std::filesystem::exists("/dev/full")) {
            FileSink full{"/dev/full"};
            expectThrows<std::system_error>([&] {
                full.append(std::string(1024 * 1024, 'x'));
                full.flush();
            });
            FileSink bufferedFull{"/dev/full"};
            expectThrows<std::system_error>([&] {
                bufferedFull.append("small");
                bufferedFull.flush();
            });
        }
#endif

        const auto regularFile = temporary.path() / "not-a-directory";
        { std::ofstream file{regularFile}; CHECK(static_cast<bool>(file)); }
        expectThrows<std::system_error>([&] {
            FileSink invalid{regularFile / "child.log"};
        });

        const auto integratedPath = temporary.path() / "logger.log";
        auto sink = std::make_shared<FileSink>(integratedPath);
        Logger::setLevel(LogLevel::info);
        Logger::setOutput(
            [sink](std::string_view message) { sink->append(message); },
            [sink] { sink->flush(); });
        const auto location = std::source_location::current();
        CHECK(Logger::log(LogLevel::info, "file-backed", location));
        CHECK(Logger::logf(LogLevel::warn, location, "fd={}", 42));
        Logger::flush();
        const auto logged = readAll(integratedPath);
        CHECK(logged.find("] [INFO] file-backed - ") != std::string::npos);
        CHECK(logged.find("] [WARN] fd=42 - ") != std::string::npos);
        CHECK(logged.ends_with('\n'));
        restoreStdoutOutput();
        Logger::setLevel(LogLevel::info);
    });
}
