#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "TestSupport.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unistd.h>

static_assert(!std::is_copy_constructible_v<Channel>);
static_assert(!std::is_move_constructible_v<Channel>);
static_assert(!std::is_move_constructible_v<EventLoop>);

int main() {
    return runTests([] {
        EventLoop loop;
        CHECK(loop.isInLoopThread());
        Socket socket;
        Channel channel{loop, socket.fd()};
        CHECK(&channel.ownerLoop() == &loop);
        CHECK(!channel.isReading() && !channel.isWriting());
        expectThrows<std::invalid_argument>([&] { Channel invalid{loop, -1}; });
        bool called = false;
        channel.setReadCallback([&] { called = true; });
        channel.setWriteCallback([&] { called = true; });
        channel.disableAll();
        channel.remove();
        channel.enableReading(); // ADD
        channel.enableReading(); // no-op
        channel.enableWriting(); // MOD
        CHECK(channel.isReading() && channel.isWriting());
        CHECK(!called); // Interest changes never execute callbacks.
        expectThrows<std::logic_error>([&] { channel.remove(); });

        Channel duplicate{loop, socket.fd()};
        expectThrows<std::logic_error>([&] { duplicate.enableReading(); });
        CHECK(!duplicate.isReading());
        expectThrows<std::logic_error>([&] { duplicate.remove(); });

        EventLoop other;
        expectThrows<std::logic_error>([&] { other.updateChannel(channel); });
        expectThrows<std::logic_error>([&] { other.removeChannel(channel); });
        std::atomic<int> rejected{0};
        std::jthread wrongThread([&] {
            if (!loop.isInLoopThread()) { ++rejected; }
            try { channel.enableReading(); } catch (const std::logic_error&) { ++rejected; }
            try { channel.disableAll(); } catch (const std::logic_error&) { ++rejected; }
            try { channel.remove(); } catch (const std::logic_error&) { ++rejected; }
        });
        wrongThread.join();
        CHECK(rejected == 4);
        CHECK(channel.isReading() && channel.isWriting());

        channel.disableReading(); // MOD: write only
        CHECK(!channel.isReading() && channel.isWriting());
        channel.disableWriting(); // DEL
        channel.remove();
        duplicate.enableReading(); // ADD works after previous registration removed
        duplicate.disableAll();
        duplicate.remove();
        channel.enableWriting(); // Re-register the original object.
        channel.enableReading();
        channel.disableAll();
        channel.remove();
        CHECK(::fcntl(socket.fd(), F_GETFD) >= 0);

        // Regular files cannot be monitored by epoll. Failed ADD must leave
        // both Channel interest and the Poller's identity map unchanged.
        const auto closeFile = [](std::FILE* file) { (void)std::fclose(file); };
        std::unique_ptr<std::FILE, decltype(closeFile)> file{std::tmpfile(), closeFile};
        CHECK(file);
        const int fd = ::fileno(file.get());
        Channel unsupported{loop, fd};
        expectThrows<std::system_error>([&] { unsupported.enableReading(); });
        CHECK(!unsupported.isReading());
        Channel retry{loop, fd};
        expectThrows<std::system_error>([&] { retry.enableWriting(); });
        CHECK(!retry.isWriting());
        unsupported.remove();
        retry.remove();
    });
}
