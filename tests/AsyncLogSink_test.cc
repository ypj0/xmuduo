#include "AsyncLogSink.h"
#include "Logger.h"
#include "TestSupport.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        auto name = (std::filesystem::temp_directory_path() / "async-test-XXXXXX").string();
        const auto result = ::mkdtemp(name.data());
        if (!result) { throw std::runtime_error("mkdtemp failed"); }
        path = result;
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
std::string readAll(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    CHECK(file);
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
}

int main() {
    return runTests([] {
        TemporaryDirectory dir;
        expectThrows<std::invalid_argument>([&] { AsyncLogSink s{dir.path, "invalid", 100, 0}; });
        expectThrows<std::invalid_argument>([&] { AsyncLogSink s{dir.path, "invalid", 100, 1024, -1ms}; });
        CHECK(!std::filesystem::exists(dir.path / "invalid.0.log"));
        {
            AsyncLogSink sink{dir.path, "copy", 1000, 256, 0ms};
            std::string message{"abc\0def", 7};
            sink.append(message);
            message.assign("changed");
            sink.append("");
            sink.flush();
            CHECK(readAll(dir.path / "copy.0.log") == std::string("abc\0def", 7));
            expectThrows<std::length_error>([&] { sink.append(std::string(256, 'x')); });
            sink.append("end");
            sink.stop();
            sink.stop();
            CHECK(readAll(dir.path / "copy.0.log") == std::string("abc\0defend", 10));
            expectThrows<std::runtime_error>([&] { sink.append("late"); });
            expectThrows<std::runtime_error>([&] { sink.flush(); });
        }
        {
            AsyncLogSink sink{dir.path, "drain", 4, 128, 1h};
            sink.append("1234");
            sink.append("56789");
            sink.append("end");
        }
        CHECK(readAll(dir.path / "drain.0.log") == "1234");
        CHECK(readAll(dir.path / "drain.1.log") == "56789");
        CHECK(readAll(dir.path / "drain.2.log") == "end");
        {
            AsyncLogSink sink{dir.path, "periodic", 1000, 256, 10ms};
            sink.append("idle");
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (readAll(dir.path / "periodic.0.log") != "idle" &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(2ms);
            }
            CHECK(readAll(dir.path / "periodic.0.log") == "idle");
        }
        {
            // Small queue exercises capacity waits; concurrent flush callers
            // must observe all their previously accepted records.
            AsyncLogSink sink{dir.path, "concurrent", 1000000, 128, 1ms};
            std::atomic<bool> failed{false};
            std::vector<std::jthread> writers;
            for (int id = 0; id < 4; ++id) {
                writers.emplace_back([&, id] {
                    try {
                        for (int n = 0; n < 200; ++n) {
                            sink.append(std::to_string(id) + " " + std::to_string(n) + "\n");
                            if (n % 20 == 0) { sink.flush(); }
                        }
                    } catch (...) { failed = true; }
                });
            }
            for (auto& writer : writers) { writer.join(); }
            CHECK(!failed);
            sink.flush();
            std::istringstream input{readAll(dir.path / "concurrent.0.log")};
            std::vector<int> next(4, 0);
            int id = 0;
            int n = 0;
            int count = 0;
            while (input >> id >> n) {
                CHECK(id >= 0 && id < 4);
                CHECK(n == next[id]++);
                ++count;
            }
            CHECK(count == 800);
        }
        {
            const auto original = dir.path / "failure";
            const auto moved = dir.path / "moved";
            std::filesystem::create_directory(original);
            AsyncLogSink sink{original, "error", 3, 128, 0ms};
            sink.append("abc");
            sink.flush();
            std::filesystem::rename(original, moved);
            sink.append("def"); // next rotation cannot create its file
            expectThrows<std::system_error>([&] { sink.flush(); });
            expectThrows<std::system_error>([&] { sink.append("next"); });
            expectThrows<std::system_error>([&] { sink.stop(); });
            CHECK(readAll(moved / "error.0.log") == "abc");
        }
        {
            auto sink = std::make_shared<AsyncLogSink>(dir.path, "logger", 10000);
            Logger::setOutput([sink](std::string_view message) { sink->append(message); },
                              [sink] { sink->flush(); });
            LOG_INFO("async record {}", 42);
            Logger::flush();
            CHECK(readAll(dir.path / "logger.0.log").find("async record 42") != std::string::npos);
            Logger::setOutput([](std::string_view) {}, [] {});
            sink->stop();
        }
    });
}
