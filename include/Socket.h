#pragma once

#include "InetAddress.h"
#include "noncopyable.h"
#include <optional>

// Linux TCP descriptor owner. fd() is borrowed; never close it externally.
class Socket : noncopyable {
public:
    enum class Family { ipv4, ipv6 };
    explicit Socket(Family family = Family::ipv4);
    ~Socket() noexcept;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] int fd() const noexcept { return fd_; }
    void bind(const InetAddress& address);
    void listen(int backlog = SOMAXCONN);
    [[nodiscard]] std::optional<Socket> accept();
    void shutdownWrite();
    [[nodiscard]] InetAddress localAddress() const;
    [[nodiscard]] InetAddress peerAddress() const;

    void setReuseAddress(bool enabled);
    void setReusePort(bool enabled);
    void setKeepAlive(bool enabled);
    void setTcpNoDelay(bool enabled);
    void setIPv6Only(bool enabled);

private:
    struct AdoptFd {};
    Socket(int fd, AdoptFd) noexcept : fd_(fd) {}
    void setOption(int level, int option, bool enabled);
    int fd_ = -1;
};
