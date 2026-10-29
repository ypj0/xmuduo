#include "RollingFileSink.h"
#include "Logger.h"
#include "TestSupport.h"

#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>
#include <thread>
#include <vector>

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        auto pattern = (std::filesystem::temp_directory_path() / "rolling-test-XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        const auto* created = ::mkdtemp(name.data());
        if (!created) { throw std::runtime_error("mkdtemp failed"); }
        path = created;
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

std::string readAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
static_assert(!std::is_copy_constructible_v<RollingFileSink>);
}

int main() {
    return runTests([] {
        TemporaryDirectory dir;
        {
            RollingFileSink sink{dir.path, "server", 5};
            sink.append("ab");
            sink.append("cde"); // Exactly the threshold stays in this file.
            sink.append("");
            sink.flush();
            CHECK(readAll(dir.path / "server.0.log") == "abcde");
            CHECK(!std::filesystem::exists(dir.path / "server.1.log"));
            sink.append("123456"); // Oversized record must remain whole.
            CHECK(readAll(dir.path / "server.0.log") == "abcde");
            sink.append(std::string_view{"x\0y", 3});
            CHECK(readAll(dir.path / "server.1.log") == "123456");
            sink.append("zzz"); // Roll before a record that does not fit.
            CHECK(readAll(dir.path / "server.2.log") == std::string("x\0y", 3));
        }
        CHECK(readAll(dir.path / "server.3.log") == "zzz");
        {
            RollingFileSink restarted{dir.path, "server", 5};
            RollingFileSink another{dir.path, "server", 5};
            restarted.append("first");
            another.append("other");
        }
        CHECK(readAll(dir.path / "server.0.log") == "abcde");
        CHECK(readAll(dir.path / "server.4.log") == "first");
        CHECK(readAll(dir.path / "server.5.log") == "other");

        for (const auto& invalid : {std::string{}, std::string{"."}, std::string{".."},
                                  std::string{"a/b"}, std::string{"a\0b", 3}}) {
            expectThrows<std::invalid_argument>([&] { RollingFileSink s{dir.path, invalid, 5}; });
        }
        expectThrows<std::invalid_argument>([&] { RollingFileSink s{dir.path, "zero", 0}; });
        expectThrows<std::invalid_argument>([&] {
            RollingFileSink s{dir.path, "negative-interval", 5, std::chrono::milliseconds{-1}};
        });
        expectThrows<std::system_error>([&] { RollingFileSink s{dir.path / "missing", "x", 5}; });

        // The background worker flushes stdio data without an explicit call.
        {
            RollingFileSink periodic{dir.path, "periodic", 1024,
                                     std::chrono::milliseconds{20}};
            periodic.append("visible after timer");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            while (std::chrono::steady_clock::now() < deadline &&
                   readAll(dir.path / "periodic.0.log") != "visible after timer") {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }
            CHECK(readAll(dir.path / "periodic.0.log") == "visible after timer");
        }
        {
            // A zero interval disables the worker; explicit flushing still works.
            RollingFileSink manual{dir.path, "manual", 1024, std::chrono::milliseconds{0}};
            manual.append("manual flush");
            manual.flush();
            CHECK(readAll(dir.path / "manual.0.log") == "manual flush");
        }
        {
            RollingFileSink concurrent{dir.path, "concurrent", 31,
                                       std::chrono::milliseconds{1}};
            std::vector<std::jthread> writers;
            for (int i = 0; i < 4; ++i) {
                writers.emplace_back([&] {
                    for (int n = 0; n < 100; ++n) { concurrent.append("record\n"); }
                });
            }
            for (auto& writer : writers) { writer.join(); }
            concurrent.flush();
            std::size_t records = 0;
            for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
                if (!entry.path().filename().string().starts_with("concurrent.")) { continue; }
                auto content = readAll(entry.path());
                CHECK(content.size() <= 31);
                while (!content.empty()) {
                    CHECK(content.starts_with("record\n"));
                    content.erase(0, 7);
                    ++records;
                }
            }
            CHECK(records == 400);
        }
        const auto shutdownStart = std::chrono::steady_clock::now();
        {
            RollingFileSink longInterval{dir.path, "shutdown", 1024, std::chrono::hours{1}};
            longInterval.append("flushed on destruction");
        }
        CHECK(std::chrono::steady_clock::now() - shutdownStart < std::chrono::seconds{5});
        CHECK(readAll(dir.path / "shutdown.0.log") == "flushed on destruction");

        // A failed rotation must not discard the old file or the rejected record's prefix.
        const auto subdir = dir.path / "failure";
        const auto moved = dir.path / "moved";
        std::filesystem::create_directory(subdir);
        {
            RollingFileSink sink{subdir, "x", 3};
            sink.append("abc");
            std::filesystem::rename(subdir, moved);
            expectThrows<std::system_error>([&] { sink.append("def"); });
            sink.flush();
            CHECK(readAll(moved / "x.0.log") == "abc");
            std::filesystem::rename(moved, subdir);
            sink.append("def");
            sink.flush();
            CHECK(readAll(subdir / "x.2.log") == "def");
        }

        auto sink = std::make_shared<RollingFileSink>(dir.path, "logger", 1);
        Logger::setOutput([sink](std::string_view msg) { sink->append(msg); },
                          [sink] { sink->flush(); });
        CHECK(Logger::log(LogLevel::info, "first record"));
        CHECK(Logger::log(LogLevel::info, "second record"));
        Logger::flush();
        CHECK(readAll(dir.path / "logger.0.log").find("first record") != std::string::npos);
        CHECK(readAll(dir.path / "logger.1.log").find("second record") != std::string::npos);
        Logger::setOutput([](std::string_view) {}, [] {});
    });
}
