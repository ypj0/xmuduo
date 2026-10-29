#include "Socket.h"
#include "TestSupport.h"

#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <system_error>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<Socket>);
static_assert(!std::is_copy_assignable_v<Socket>);
static_assert(std::is_nothrow_move_constructible_v<Socket>);
static_assert(std::is_nothrow_move_assignable_v<Socket>);

namespace {
void checkFlags(const Socket& socket) {
    const int flags = ::fcntl(socket.fd(), F_GETFL);
    CHECK(flags >= 0 && (flags & O_NONBLOCK));
    const int descriptorFlags = ::fcntl(socket.fd(), F_GETFD);
    CHECK(descriptorFlags >= 0 && (descriptorFlags & FD_CLOEXEC));
}
int option(const Socket& socket, int level, int name) {
    int value = -1;
    socklen_t length = sizeof(value);
    CHECK(::getsockopt(socket.fd(), level, name, &value, &length) == 0);
    return value;
}
void waitFor(const Socket& socket, short events) {
    pollfd descriptor{socket.fd(), events, 0};
    int result;
    do { result = ::poll(&descriptor, 1, 2000); } while (result < 0 && errno == EINTR);
    CHECK(result == 1);
    CHECK(descriptor.revents & events);
}
void exerciseConnection(Socket& listener, Socket::Family family, const InetAddress& address) {
    listener.bind(address);
    listener.listen();
    CHECK(!listener.accept());
    const auto bound = listener.localAddress();
    CHECK(bound.port() != 0);
    CHECK(bound.ip() == address.ip());
    Socket client{family};
    const int result = ::connect(client.fd(), bound.sockaddrPtr(), bound.sockaddrLength());
    CHECK(result == 0 || (result == -1 && errno == EINPROGRESS));
    waitFor(client, POLLOUT);
    CHECK(option(client, SOL_SOCKET, SO_ERROR) == 0);
    waitFor(listener, POLLIN);
    auto accepted = listener.accept();
    CHECK(accepted.has_value());
    checkFlags(*accepted);
    CHECK(accepted->peerAddress().ipPort() == client.localAddress().ipPort());
    CHECK(accepted->localAddress().ipPort() == bound.ipPort());
    CHECK(!listener.accept());

    CHECK(::send(client.fd(), "a", 1, MSG_NOSIGNAL) == 1);
    waitFor(*accepted, POLLIN);
    char byte = 0;
    CHECK(::recv(accepted->fd(), &byte, 1, 0) == 1 && byte == 'a');
    accepted->shutdownWrite();
    waitFor(client, POLLIN);
    CHECK(::recv(client.fd(), &byte, 1, 0) == 0);
    // SHUT_WR still permits receiving data from the peer.
    CHECK(::send(client.fd(), "b", 1, MSG_NOSIGNAL) == 1);
    waitFor(*accepted, POLLIN);
    CHECK(::recv(accepted->fd(), &byte, 1, 0) == 1 && byte == 'b');
}
}

int main() {
    return runTests([] {
        int closedFd;
        {
            Socket source;
            checkFlags(source);
            closedFd = source.fd();
            Socket moved{std::move(source)};
            CHECK(source.fd() == -1 && moved.fd() == closedFd);
            Socket destination;
            const int replaced = destination.fd();
            destination = std::move(moved);
            CHECK(moved.fd() == -1 && destination.fd() == closedFd);
            CHECK(::fcntl(replaced, F_GETFD) == -1 && errno == EBADF);
            Socket& alias = destination;
            destination = std::move(alias);
            CHECK(destination.fd() == closedFd);
            expectThrows<std::system_error>([&] { (void)source.localAddress(); });
        }
        CHECK(::fcntl(closedFd, F_GETFD) == -1 && errno == EBADF);
        expectThrows<std::invalid_argument>([] { Socket invalid{static_cast<Socket::Family>(9)}; });
        Socket listener;
        expectThrows<std::invalid_argument>([&] { listener.listen(0); });
        expectThrows<std::system_error>([&] { (void)listener.accept(); });
        expectThrows<std::system_error>([&] { (void)listener.peerAddress(); });
        for (bool enabled : {true, false}) {
            listener.setReuseAddress(enabled);
            listener.setReusePort(enabled);
            listener.setKeepAlive(enabled);
            listener.setTcpNoDelay(enabled);
            CHECK(option(listener, SOL_SOCKET, SO_REUSEADDR) == enabled);
            CHECK(option(listener, SOL_SOCKET, SO_REUSEPORT) == enabled);
            CHECK(option(listener, SOL_SOCKET, SO_KEEPALIVE) == enabled);
            CHECK(option(listener, IPPROTO_TCP, TCP_NODELAY) == enabled);
        }
        exerciseConnection(listener, Socket::Family::ipv4, InetAddress::loopbackIPv4());
        Socket duplicate;
        try {
            duplicate.bind(listener.localAddress());
            CHECK(false);
        } catch (const std::system_error& error) {
            CHECK(error.code() == std::errc::address_in_use);
        }
        // Skip only when the host explicitly lacks IPv6 loopback support.
        std::optional<Socket> ipv6;
        try { ipv6.emplace(Socket::Family::ipv6); }
        catch (const std::system_error& error) {
            if (error.code().value() != EAFNOSUPPORT && error.code().value() != EPROTONOSUPPORT) { throw; }
            std::cout << "IPv6 unavailable; IPv6 connection test skipped\n";
        }
        if (ipv6) {
            ipv6->setIPv6Only(true);
            CHECK(option(*ipv6, IPPROTO_IPV6, IPV6_V6ONLY) == 1);
            exerciseConnection(*ipv6, Socket::Family::ipv6, InetAddress::loopbackIPv6());
        }
    });
}
