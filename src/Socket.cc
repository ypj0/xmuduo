#include "Socket.h"

#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace {
[[noreturn]] void fail(const char* operation) {
    const int error = errno;
    throw std::system_error(error, std::generic_category(), operation);
}
void closeFd(int fd) noexcept {
    // On Linux do not retry close(EINTR): the fd may already be reused.
    const int saved = errno;
    if (fd >= 0) { (void)::close(fd); }
    errno = saved;
}
}

Socket::Socket(Family family) {
    int domain;
    switch (family) {
    case Family::ipv4: domain = AF_INET; break;
    case Family::ipv6: domain = AF_INET6; break;
    default: throw std::invalid_argument("Invalid socket family");
    }
    fd_ = ::socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd_ < 0) { fail("socket"); }
}

Socket::~Socket() noexcept { closeFd(fd_); }
Socket::Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        closeFd(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

void Socket::bind(const InetAddress& address) {
    if (::bind(fd_, address.sockaddrPtr(), address.sockaddrLength()) < 0) { fail("bind"); }
}
void Socket::listen(int backlog) {
    if (backlog <= 0) { throw std::invalid_argument("Listen backlog must be positive"); }
    if (::listen(fd_, backlog) < 0) { fail("listen"); }
}
std::optional<Socket> Socket::accept() {
    for (;;) {
        const int accepted = ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (accepted >= 0) { return Socket{accepted, AdoptFd{}}; }
        if (errno == EINTR) { continue; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) { return std::nullopt; }
        fail("accept4");
    }
}
void Socket::shutdownWrite() {
    if (::shutdown(fd_, SHUT_WR) < 0) { fail("shutdown"); }
}
InetAddress Socket::localAddress() const {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        fail("getsockname");
    }
    return InetAddress{reinterpret_cast<const sockaddr*>(&address), length};
}
InetAddress Socket::peerAddress() const {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        fail("getpeername");
    }
    return InetAddress{reinterpret_cast<const sockaddr*>(&address), length};
}
void Socket::setOption(int level, int option, bool enabled) {
    const int value = enabled ? 1 : 0;
    if (::setsockopt(fd_, level, option, &value, sizeof(value)) < 0) { fail("setsockopt"); }
}
void Socket::setReuseAddress(bool enabled) { setOption(SOL_SOCKET, SO_REUSEADDR, enabled); }
void Socket::setReusePort(bool enabled) { setOption(SOL_SOCKET, SO_REUSEPORT, enabled); }
void Socket::setKeepAlive(bool enabled) { setOption(SOL_SOCKET, SO_KEEPALIVE, enabled); }
void Socket::setTcpNoDelay(bool enabled) { setOption(IPPROTO_TCP, TCP_NODELAY, enabled); }
void Socket::setIPv6Only(bool enabled) { setOption(IPPROTO_IPV6, IPV6_V6ONLY, enabled); }
