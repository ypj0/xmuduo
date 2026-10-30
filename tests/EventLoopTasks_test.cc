#include "EventLoop.h"
#include "Socket.h"
#include "TestSupport.h"
#include <future>
#include <atomic>

using namespace std::chrono_literals;
int main() {
    return runTests([] {
        EventLoop loop;
        int count = 0;
        loop.runInLoop([&] { ++count; });
        CHECK(count == 1);
        loop.queueInLoop([&] {
            ++count;
            expectThrows<std::logic_error>([&] { loop.runOnce(0ms); });
            expectThrows<std::logic_error>([&] { loop.loop(); });
            loop.queueInLoop([&] { ++count; });
        });
        CHECK(count == 1);
        loop.runOnce(0ms);
        CHECK(count == 2);
        loop.runOnce(0ms);
        CHECK(count == 3);
        loop.queueInLoop([] { throw std::runtime_error("task error"); });
        loop.queueInLoop([&] { ++count; });
        expectThrows<std::runtime_error>([&] { loop.runOnce(0ms); });
        CHECK(count == 4);
        loop.runOnce(0ms);
        expectThrows<std::invalid_argument>([&] { loop.queueInLoop({}); });
        expectThrows<std::invalid_argument>([&] { loop.runInLoop({}); });

        Socket socket;
        auto channel = std::make_unique<Channel>(loop, socket.fd());
        // Task stage may unregister and destroy a Channel.
        channel->enableReading();
        loop.queueInLoop([&] {
            channel->disableAll();
            channel->remove();
            channel.reset();
        });
        loop.runOnce(0ms);
        CHECK(!channel);

        channel = std::make_unique<Channel>(loop, socket.fd());
        bool deferred = false;
        channel->setWriteCallback([&] {
            channel->disableAll();
            loop.queueInLoop([&] {
                channel->remove();
                channel.reset();
                deferred = true;
            });
            CHECK(!deferred);
        });
        channel->enableWriting();
        loop.runOnce(100ms);
        CHECK(deferred && !channel);

        std::promise<EventLoop*> published;
        auto ready = published.get_future();
        std::promise<void> exited;
        auto done = exited.get_future();
        std::promise<void> release;
        auto releaseFuture = release.get_future();
        std::jthread thread([&] {
            try {
                EventLoop background;
                published.set_value(&background);
                background.loop();
                exited.set_value();
                // Keep the object alive until cross-thread callers have returned.
                releaseFuture.wait();
            } catch (...) { exited.set_exception(std::current_exception()); }
        });
        auto* background = ready.get();
        std::promise<bool> executed;
        auto execution = executed.get_future();
        background->runInLoop([&] { executed.set_value(background->isInLoopThread()); });
        const bool timely = execution.wait_for(2s) == std::future_status::ready;
        background->quit();
        const bool stopped = done.wait_for(2s) == std::future_status::ready;
        release.set_value();
        thread.join();
        CHECK(timely && stopped);
        CHECK(execution.get());
        done.get();

        EventLoop preQuit;
        preQuit.quit();
        preQuit.loop(); // The quit flag is sticky; no restart or implicit drain.
    });
}
