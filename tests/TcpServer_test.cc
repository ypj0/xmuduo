#include "TcpServer.h"
#include "EventLoop.h"
#include "TestSupport.h"
#include <atomic>
#include <cerrno>
#include <poll.h>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace {
Socket connectTo(const InetAddress& address) {
    Socket socket{address.family() == AF_INET6 ? Socket::Family::ipv6 : Socket::Family::ipv4};
    const int result = ::connect(socket.fd(), address.sockaddrPtr(), address.sockaddrLength());
    CHECK(result == 0 || (result < 0 && errno == EINPROGRESS));
    pollfd p{socket.fd(), POLLOUT, 0};
    CHECK(::poll(&p, 1, 2000) == 1);
    int error;
    socklen_t length = sizeof(error);
    CHECK(::getsockopt(socket.fd(), SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0);
    return socket;
}
template<class Predicate> void pump(EventLoop& loop, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!done() && std::chrono::steady_clock::now() < deadline) { loop.runOnce(1ms); }
    CHECK(done());
}
bool readAvailable(Socket& socket, std::string& output) {
    char bytes[4096];
    for (;;) {
        auto n = ::recv(socket.fd(), bytes, sizeof(bytes), 0);
        if (n > 0) { output.append(bytes, static_cast<std::size_t>(n)); continue; }
        if (n == 0) { return true; }
        if (errno == EINTR) { continue; }
        CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
        return false;
    }
}
auto echo = [](const TcpConnection::Ptr& connection, Buffer& buffer) {
    connection->send(buffer.readableView());
    buffer.retrieveAll();
};
}

int main() {
    return runTests([] {
        EventLoop loop;
        {
            TcpServer::Options invalid;
            invalid.maxConnections = 0;
            expectThrows<std::invalid_argument>([&] { TcpServer s{loop, InetAddress::loopbackIPv4(), invalid}; });
        }
        {
            TcpServer server{loop, InetAddress::loopbackIPv4()};
            expectThrows<std::logic_error>([&] { server.start(); });
            expectThrows<std::invalid_argument>([&] { server.setMessageCallback({}); });
            int closed = 0;
            int stopped = 0;
            server.setMessageCallback(echo);
            server.setCloseCallback([&](const auto&, auto error) { CHECK(!error); ++closed; });
            server.setStoppedCallback([&] { CHECK(server.connectionCount() == 0); ++stopped; });
            server.start();
            server.start();
            expectThrows<std::logic_error>([&] { server.setMessageCallback(echo); });
            const auto address = server.localAddress();
            std::vector<Socket> clients;
            for (int i = 0; i < 4; ++i) { clients.push_back(connectTo(address)); }
            pump(loop, [&] { return server.connectionCount() == 4; });
            for (auto& client : clients) {
                CHECK(::send(client.fd(), "x\0y", 3, MSG_NOSIGNAL) == 3);
                client.shutdownWrite();
            }
            pump(loop, [&] { return closed == 4; });
            CHECK(server.connectionCount() == 0);
            for (auto& client : clients) {
                std::string response;
                pump(loop, [&] { return readAvailable(client, response); });
                CHECK(response == std::string("x\0y", 3));
            }
            std::atomic<bool> rejected{false};
            std::jthread thread([&] {
                try { server.stop(); } catch (const std::logic_error&) { rejected = true; }
            });
            thread.join();
            CHECK(rejected);
            server.stop();
            server.stop();
            CHECK(server.state() == TcpServer::State::stopping);
            pump(loop, [&] { return server.state() == TcpServer::State::stopped; });
            CHECK(stopped == 1);
            expectThrows<std::logic_error>([&] { server.start(); });
            // Listener has actually been closed, not merely paused.
            TcpServer replacement{loop, address};
            replacement.setMessageCallback(echo);
            replacement.start();
            replacement.stop();
            pump(loop, [&] { return replacement.state() == TcpServer::State::stopped; });
        }
        {
            TcpServer::Options config;
            config.maxConnections = 1;
            TcpServer server{loop, InetAddress::loopbackIPv4(), config};
            server.setMessageCallback([&](const auto&, Buffer&) { server.stop(); });
            TcpConnection::Ptr retained;
            int closed = 0;
            server.setCloseCallback([&](const auto& connection, auto) { retained = connection; ++closed; });
            server.start();
            auto first = connectTo(server.localAddress());
            pump(loop, [&] { return server.connectionCount() == 1; });
            auto excess = connectTo(server.localAddress());
            std::string discarded;
            pump(loop, [&] { return readAvailable(excess, discarded); });
            CHECK(server.connectionCount() == 1);
            CHECK(::send(first.fd(), "stop", 4, MSG_NOSIGNAL) == 4);
            pump(loop, [&] { return server.state() == TcpServer::State::stopped; });
            CHECK(closed == 1 && retained->state() == TcpConnection::State::closed);
        }
        {
            auto server = std::make_unique<TcpServer>(loop, InetAddress::loopbackIPv4());
            server->setMessageCallback(echo);
            bool notified = false;
            server->setCloseCallback([&](const auto&, auto) { notified = true; });
            server->start();
            auto client = connectTo(server->localAddress());
            pump(loop, [&] { return server->connectionCount() == 1; });
            server.reset(); // Cleanup survives wrapper destruction; user callbacks suppressed.
            std::string discarded;
            pump(loop, [&] { return readAvailable(client, discarded); });
            CHECK(!notified);
        }
        {
            TcpServer server{loop, InetAddress::loopbackIPv4()};
            server.setMessageCallback(echo);
            bool stopped = false;
            server.setCloseCallback([](const auto&, auto) { throw std::runtime_error("close failure"); });
            server.setStoppedCallback([&] { stopped = true; });
            server.start();
            auto client = connectTo(server.localAddress());
            pump(loop, [&] { return server.connectionCount() == 1; });
            server.stop();
            loop.runOnce(0ms); // Queue connection cleanup.
            expectThrows<std::runtime_error>([&] { loop.runOnce(0ms); });
            CHECK(stopped && server.state() == TcpServer::State::stopped);
        }
        {
            TcpServer server{loop, InetAddress::loopbackIPv6()};
            int stopped = 0;
            server.setStoppedCallback([&] { ++stopped; });
            server.stop(); // Stop before start also releases the bound endpoint.
            pump(loop, [&] { return server.state() == TcpServer::State::stopped; });
            CHECK(stopped == 1);
        }
    });
}
