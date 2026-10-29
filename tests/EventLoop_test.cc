#include "EventLoop.h"
#include "TestSupport.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <limits>
#include <thread>
#include <atomic>

namespace {
using namespace std::chrono_literals;
struct Counter {
    int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    Counter() { CHECK(fd >= 0); }
    ~Counter() { ::close(fd); }
    void signal() {
        const std::uint64_t value = 1;
        CHECK(::write(fd, &value, sizeof(value)) == sizeof(value));
    }
    void drain() {
        std::uint64_t value;
        CHECK(::read(fd, &value, sizeof(value)) == sizeof(value));
    }
};
}
int main() {
    return runTests([] {
        EventLoop loop;
        Counter counter;
        Channel channel{loop, counter.fd};
        int reads = 0;
        channel.setReadCallback([&] { counter.drain(); ++reads; });
        channel.enableReading();
        loop.runOnce(0ms);
        CHECK(reads == 0);
        counter.signal();
        loop.runOnce(100ms);
        CHECK(reads == 1);
        loop.runOnce(0ms);
        CHECK(reads == 1);
        channel.setReadCallback([&] {
            counter.drain();
            expectThrows<std::logic_error>([&] { loop.runOnce(0ms); });
            channel.disableAll();
            expectThrows<std::logic_error>([&] { channel.remove(); });
            ++reads;
        });
        counter.signal();
        loop.runOnce(100ms);
        CHECK(reads == 2);
        channel.remove();
        counter.signal();
        loop.runOnce(0ms);
        CHECK(reads == 2);
        channel.enableReading();
        channel.setReadCallback([&] {
            counter.drain();
            throw std::runtime_error("callback failure");
        });
        expectThrows<std::runtime_error>([&] { loop.runOnce(100ms); });
        channel.disableAll();
        channel.remove();
        loop.runOnce(0ms); // Dispatch state was restored after exception.
        expectThrows<std::invalid_argument>([&] { loop.runOnce(-1ms); });
        expectThrows<std::invalid_argument>([&] {
            loop.runOnce(std::chrono::milliseconds{std::numeric_limits<int>::max()} + 1ms);
        });
        std::atomic<bool> rejected{false};
        std::jthread worker([&] {
            try { loop.runOnce(0ms); } catch (const std::logic_error&) { rejected = true; }
        });
        worker.join();
        CHECK(rejected);

        int writes = 0;
        channel.setWriteCallback([&] { ++writes; channel.disableWriting(); });
        channel.enableWriting();
        loop.runOnce(100ms); // eventfd is writable while its counter isn't full.
        CHECK(writes == 1);
        channel.remove();

        // Synthetic combinations exercise dispatch order independently of Linux timing.
        std::string order;
        channel.setReadCallback([&] { order += 'r'; });
        channel.setWriteCallback([&] { order += 'w'; });
        channel.setErrorCallback([&] { order += 'e'; });
        channel.setCloseCallback([&] { order += 'c'; });
        channel.enableReading();
        channel.enableWriting();
        channel.handleEvent({.readable=true, .writable=true, .hangup=true, .error=true});
        CHECK(order == "erw");
        order.clear();
        channel.handleEvent({.peerClosed=true});
        CHECK(order == "r");
        order.clear();
        channel.handleEvent({.hangup=true});
        CHECK(order == "c");
        channel.disableAll();
        channel.remove();

        std::vector<std::unique_ptr<Counter>> counters;
        std::vector<std::unique_ptr<Channel>> channels;
        int total = 0;
        for (int i = 0; i < 40; ++i) {
            counters.push_back(std::make_unique<Counter>());
            auto* c = counters.back().get();
            auto item = std::make_unique<Channel>(loop, c->fd);
            item->setReadCallback([&, c] { c->drain(); ++total; });
            item->enableReading();
            c->signal();
            channels.push_back(std::move(item));
        }
        loop.runOnce(100ms);
        CHECK(total == 40);
        for (auto& item : channels) { item->disableAll(); item->remove(); }
    });
}
