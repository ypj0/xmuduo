#include "Acceptor.h"
#include "EventLoop.h"
#include "TestSupport.h"
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <thread>
#include <system_error>
#include <type_traits>
#include <vector>

using namespace std::chrono_literals;
static_assert(!std::is_copy_constructible_v<Acceptor>);
static_assert(!std::is_move_constructible_v<Acceptor>);

namespace {
Socket connectTo(const InetAddress& address) {
    Socket socket{address.family() == AF_INET6 ? Socket::Family::ipv6 : Socket::Family::ipv4};
    const int result = ::connect(socket.fd(), address.sockaddrPtr(), address.sockaddrLength());
    CHECK(result == 0 || (result < 0 && errno == EINPROGRESS));
    pollfd p{socket.fd(), POLLOUT, 0};
    CHECK(::poll(&p, 1, 2000) == 1);
    int error = -1;
    socklen_t length = sizeof(error);
    CHECK(::getsockopt(socket.fd(), SOL_SOCKET, SO_ERROR, &error, &length) == 0);
    CHECK(error == 0);
    return socket;
}
}

int main() {
    return runTests([] {
        EventLoop loop;
        Acceptor acceptor{loop, InetAddress::loopbackIPv4()};
        CHECK(acceptor.localAddress().port() != 0);
        CHECK(!acceptor.accepting());
        expectThrows<std::logic_error>([&] { acceptor.start(); });
        expectThrows<std::invalid_argument>([&] { acceptor.setNewConnectionCallback({}); });
        std::vector<Socket> connections;
        acceptor.setNewConnectionCallback([&](Socket socket, const InetAddress& peer) {
            CHECK(loop.isInLoopThread());
            CHECK(socket.peerAddress().ipPort() == peer.ipPort());
            CHECK(::fcntl(socket.fd(), F_GETFL) & O_NONBLOCK);
            CHECK(::fcntl(socket.fd(), F_GETFD) & FD_CLOEXEC);
            connections.push_back(std::move(socket));
        });
        expectThrows<std::invalid_argument>([&] { acceptor.start(0); });
        acceptor.start(128);
        acceptor.start();
        CHECK(acceptor.accepting());
        loop.runOnce(0ms);
        CHECK(connections.empty());
        std::vector<Socket> clients;
        for (int i = 0; i < 70; ++i) { clients.push_back(connectTo(acceptor.localAddress())); }
        bool taskRan = false;
        loop.queueInLoop([&] { taskRan = true; });
        loop.runOnce(100ms);
        CHECK(connections.size() == 64 && taskRan);
        loop.runOnce(100ms);
        CHECK(connections.size() == 70);

        acceptor.stop();
        acceptor.stop();
        auto queued = connectTo(acceptor.localAddress());
        loop.runOnce(0ms);
        CHECK(connections.size() == 70);
        acceptor.start();
        loop.runOnce(100ms);
        CHECK(connections.size() == 71);
        // Pausing acceptance does not close already-transferred sockets.
        acceptor.stop();
        CHECK(::send(clients[0].fd(), "x", 1, MSG_NOSIGNAL) == 1);
        pollfd p{connections[0].fd(), POLLIN, 0};
        CHECK(::poll(&p, 1, 2000) == 1);
        char byte;
        CHECK(::recv(connections[0].fd(), &byte, 1, 0) == 1 && byte == 'x');

        std::atomic<int> rejected{0};
        std::jthread wrongThread([&] {
            try { acceptor.start(); } catch (const std::logic_error&) { ++rejected; }
            try { acceptor.stop(); } catch (const std::logic_error&) { ++rejected; }
            try { (void)acceptor.localAddress(); } catch (const std::logic_error&) { ++rejected; }
        });
        wrongThread.join();
        CHECK(rejected == 3);

        int failedFd = -1;
        acceptor.setNewConnectionCallback([&](Socket socket, const InetAddress&) {
            failedFd = socket.fd();
            throw std::runtime_error("callback failed");
        });
        acceptor.start();
        auto failedClient = connectTo(acceptor.localAddress());
        expectThrows<std::runtime_error>([&] { loop.runOnce(100ms); });
        CHECK(!acceptor.accepting());
        CHECK(failedFd >= 0 && ::fcntl(failedFd, F_GETFD) < 0 && errno == EBADF);

        acceptor.setNewConnectionCallback([&](Socket, const InetAddress&) {
            expectThrows<std::logic_error>([&] {
                acceptor.setNewConnectionCallback([](Socket, const InetAddress&) {});
            });
            acceptor.stop(); // Legal during the connection callback.
        });
        acceptor.start();
        auto stopClient = connectTo(acceptor.localAddress());
        loop.runOnce(100ms);
        CHECK(!acceptor.accepting());

        // Destruction is deferred until the entire active-event batch is over.
        auto deferred = std::make_unique<Acceptor>(loop, InetAddress::loopbackIPv4());
        deferred->setNewConnectionCallback([&](Socket, const InetAddress&) {
            loop.queueInLoop([&] { deferred.reset(); });
        });
        deferred->start();
        auto finalClient = connectTo(deferred->localAddress());
        loop.runOnce(100ms);
        CHECK(!deferred);
        loop.runOnce(0ms);

        Acceptor ipv6{loop, InetAddress::loopbackIPv6()};
        bool received6 = false;
        ipv6.setNewConnectionCallback([&](Socket, const InetAddress& peer) {
            CHECK(peer.family() == AF_INET6 && peer.ip() == "::1");
            received6 = true;
        });
        ipv6.start();
        auto client6 = connectTo(ipv6.localAddress());
        loop.runOnce(100ms);
        CHECK(received6);
    });
}
