#include "EventLoop.h"
#include "TcpServer.h"

#include <charconv>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <cerrno>

namespace {
volatile std::sig_atomic_t stopRequested = 0;

extern "C" void requestStop(int) { stopRequested = 1; }

class StopSignals {
public:
    StopSignals() {
        struct sigaction action{};
        action.sa_handler = requestStop;
        ::sigemptyset(&action.sa_mask);
        if (::sigaction(SIGINT, &action, &oldInt_) < 0) {
            throw std::system_error(errno, std::generic_category(), "sigaction SIGINT");
        }
        if (::sigaction(SIGTERM, &action, &oldTerm_) < 0) {
            const int error = errno;
            (void)::sigaction(SIGINT, &oldInt_, nullptr);
            throw std::system_error(error, std::generic_category(), "sigaction SIGTERM");
        }
    }
    ~StopSignals() {
        (void)::sigaction(SIGTERM, &oldTerm_, nullptr);
        (void)::sigaction(SIGINT, &oldInt_, nullptr);
    }
private:
    struct sigaction oldInt_{};
    struct sigaction oldTerm_{};
};

std::uint16_t parsePort(std::string_view text) {
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value > 65535) {
        throw std::invalid_argument("Port must be an integer from 0 to 65535");
    }
    return static_cast<std::uint16_t>(value);
}

void stopAndDrain(EventLoop& loop, TcpServer& server) {
    server.stop();
    while (server.state() != TcpServer::State::stopped) {
        loop.runOnce(std::chrono::milliseconds{0});
    }
}
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << "Usage: echo_server [numeric-ip [port]]\n"
                     "Default: 127.0.0.1 8080; port 0 selects a free port.\n";
        return 0;
    }
    if (argc > 3) {
        std::cerr << "Usage: echo_server [numeric-ip [port]]\n";
        return 1;
    }
    try {
        const std::string_view ip = argc > 1 ? argv[1] : "127.0.0.1";
        const auto port = argc > 2 ? parsePort(argv[2]) : std::uint16_t{8080};
        StopSignals signals;
        EventLoop loop;
        TcpServer server{loop, InetAddress{port, ip}};
        // Keep the loop alive until all deferred cleanup has run, including
        // when configuring or processing the server throws.
        try {
            server.setMessageCallback([](const TcpConnection::Ptr& connection, Buffer& input) {
                connection->send(input.readableView());
                input.retrieveAll();
            });
            server.setCloseCallback([](const TcpConnection::Ptr& connection, std::error_code error) {
                if (error) {
                    std::cerr << "Connection " << connection->peerAddress().ipPort()
                              << " closed: " << error.message() << '\n';
                }
            });
            server.start();
            std::cout << "Listening on " << server.localAddress().ipPort()
                      << " (Ctrl+C to stop)" << std::endl;
            // Bounded wait makes a signal arriving just before epoll_wait safe:
            // the next flag check is at most one wait away (plus callback time).
            while (!stopRequested) {
                loop.runOnce(std::chrono::milliseconds{100});
            }
        } catch (...) {
            const auto error = std::current_exception();
            stopAndDrain(loop, server);
            std::rethrow_exception(error);
        }
        stopAndDrain(loop, server);
        std::cout << "Server stopped\n";
    } catch (const std::exception& error) {
        std::cerr << "Echo server: " << error.what() << '\n';
        return 1;
    }
}
