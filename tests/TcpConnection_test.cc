#include "TcpConnection.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "TestSupport.h"
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <thread>
#include <atomic>
#include <vector>

using namespace std::chrono_literals;
namespace {
Socket connectTo(const InetAddress& address) {
    Socket client;
    const auto result = ::connect(client.fd(), address.sockaddrPtr(), address.sockaddrLength());
    CHECK(result == 0 || (result < 0 && errno == EINPROGRESS));
    pollfd p{client.fd(), POLLOUT, 0};
    CHECK(::poll(&p, 1, 2000) == 1);
    int error = 0;
    socklen_t length = sizeof(error);
    CHECK(::getsockopt(client.fd(), SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0);
    return client;
}
std::pair<Socket, Socket> connectedPair() {
    Socket listener;
    listener.bind(InetAddress::loopbackIPv4());
    listener.listen();
    auto client = connectTo(listener.localAddress());
    pollfd pending{listener.fd(), POLLIN, 0};
    CHECK(::poll(&pending, 1, 2000) == 1);
    auto accepted = listener.accept();
    CHECK(accepted);
    return {std::move(client), std::move(*accepted)};
}
template<class Predicate> void pumpUntil(EventLoop& loop, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done() && std::chrono::steady_clock::now() < deadline) { loop.runOnce(1ms); }
    CHECK(done());
}
bool readAvailable(Socket& client, std::string& result) {
    char bytes[65536];
    for (;;) {
        const auto count = ::recv(client.fd(), bytes, sizeof(bytes), 0);
        if (count > 0) { result.append(bytes, static_cast<std::size_t>(count)); continue; }
        if (count == 0) { return true; }
        if (errno == EINTR) { continue; }
        CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
        return false;
    }
}
}

int main() {
    return runTests([] {
        EventLoop loop;
        {
            Acceptor acceptor{loop, InetAddress::loopbackIPv4()};
            TcpConnection::Ptr connection;
            int closes = 0;
            std::string request;
            acceptor.setNewConnectionCallback([&](Socket socket, const InetAddress&) {
                connection = TcpConnection::create(loop, std::move(socket),
                    [&](const auto& self, Buffer& input) {
                        request += input.readableView();
                        self->send(input.readableView());
                        input.retrieveAll();
                    }, [&](const auto&, std::error_code error) { CHECK(!error); ++closes; });
            });
            acceptor.start();
            auto client = connectTo(acceptor.localAddress());
            pumpUntil(loop, [&] { return bool(connection); });
            CHECK(connection->peerAddress().ipPort() == client.localAddress().ipPort());
            CHECK(::send(client.fd(), "a\0b", 3, MSG_NOSIGNAL) == 3);
            pumpUntil(loop, [&] { return request.size() == 3; });
            CHECK(::send(client.fd(), "cd", 2, MSG_NOSIGNAL) == 2);
            client.shutdownWrite();
            pumpUntil(loop, [&] { return closes == 1; });
            CHECK(request == std::string("a\0bcd", 5));
            CHECK(connection->state() == TcpConnection::State::closed);
            std::string response;
            pumpUntil(loop, [&] { return readAvailable(client, response); });
            CHECK(response == request);
            expectThrows<std::logic_error>([&] { connection->send("late"); });
            connection->forceClose();
            loop.runOnce(0ms);
            CHECK(closes == 1);
        }
        {
            auto [client, server] = connectedPair();
            const int small = 4096;
            CHECK(::setsockopt(server.fd(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
            int closes = 0;
            auto connection = TcpConnection::create(loop, std::move(server),
                [](const auto&, Buffer& input) { input.retrieveAll(); },
                [&](const auto&, std::error_code error) { CHECK(!error); ++closes; });
            std::string payload(2 * 1024 * 1024, 'x');
            for (std::size_t i = 0; i < payload.size(); i += 997) { payload[i] = 'z'; }
            connection->send(payload);
            CHECK(connection->pendingOutputBytes() > 0);
            connection->send("tail");
            connection->shutdown(); // SHUT_WR only after the queued bytes drain.
            expectThrows<std::logic_error>([&] { connection->send("too late"); });
            std::string response;
            pumpUntil(loop, [&] { return readAvailable(client, response); });
            CHECK(response == payload + "tail");
            CHECK(connection->pendingOutputBytes() == 0);
            CHECK(connection->state() == TcpConnection::State::disconnecting);
            client.shutdownWrite();
            pumpUntil(loop, [&] { return closes == 1; });
        }
        {
            auto [client, server] = connectedPair();
            std::error_code closedError;
            auto connection = TcpConnection::create(loop, std::move(server),
                [](const auto&, Buffer&) {},
                [&](const auto&, std::error_code error) { closedError = error; }, 8);
            expectThrows<std::length_error>([&] { connection->send("123456789"); });
            CHECK(connection->pendingOutputBytes() == 0);
            CHECK(::send(client.fd(), "123456789", 9, MSG_NOSIGNAL) == 9);
            pumpUntil(loop, [&] { return connection->state() == TcpConnection::State::closed; });
            CHECK(closedError == std::errc::no_buffer_space);
        }
        {
            auto [client, server] = connectedPair();
            int closes = 0;
            auto connection = TcpConnection::create(loop, std::move(server),
                [](const auto&, Buffer&) { throw std::runtime_error("message failed"); },
                [&](const auto&, std::error_code error) {
                    CHECK(error == std::errc::operation_canceled); ++closes;
                });
            CHECK(::send(client.fd(), "x", 1, MSG_NOSIGNAL) == 1);
            bool threw = false;
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (!threw && std::chrono::steady_clock::now() < deadline) {
                try { loop.runOnce(10ms); }
                catch (const std::runtime_error& error) {
                    CHECK(std::string_view{error.what()} == "message failed");
                    threw = true;
                }
            }
            CHECK(threw);
            loop.runOnce(0ms);
            CHECK(closes == 1);
        }
        {
            // Release the last external owner inside a message callback.
            auto [client, server] = connectedPair();
            TcpConnection::Ptr connection;
            connection = TcpConnection::create(loop, std::move(server),
                [&](const auto&, Buffer&) { connection.reset(); });
            std::weak_ptr<TcpConnection> weak = connection;
            CHECK(::send(client.fd(), "x", 1, MSG_NOSIGNAL) == 1);
            pumpUntil(loop, [&] { return weak.expired(); });
            std::string ignored;
            pumpUntil(loop, [&] { return readAvailable(client, ignored); });
        }
        {
            auto [client, server] = connectedPair();
            auto connection = TcpConnection::create(loop, std::move(server),
                [](const auto&, Buffer& input) { input.retrieveAll(); });
            std::atomic<bool> rejected{false};
            std::jthread thread([&] {
                try { connection->send("wrong thread"); }
                catch (const std::logic_error&) { rejected = true; }
            });
            thread.join();
            CHECK(rejected);
            // Cross-thread final release is routed through the loop's task queue.
            std::jthread release([owned = std::move(connection)]() mutable { owned.reset(); });
            release.join();
            loop.runOnce(0ms);
            std::string ignored;
            pumpUntil(loop, [&] { return readAvailable(client, ignored); });
        }
        {
            auto pair = connectedPair();
            std::error_code closedError;
            auto connection = TcpConnection::create(loop, std::move(pair.second),
                [](const auto&, Buffer& input) { input.retrieveAll(); },
                [&](const auto&, std::error_code error) { closedError = error; });
            // Abortive peer close exercises EPOLLERR / recv errors without SIGPIPE.
            linger reset{1, 0};
            CHECK(::setsockopt(pair.first.fd(), SOL_SOCKET, SO_LINGER, &reset, sizeof(reset)) == 0);
            { Socket closeNow{std::move(pair.first)}; }
            pumpUntil(loop, [&] { return connection->state() == TcpConnection::State::closed; });
            CHECK(closedError);
        }
    });
}
