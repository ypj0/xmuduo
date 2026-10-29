#pragma once

#include "copyable.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <sys/socket.h>

// Value type for numeric IPv4 and IPv6 socket addresses.
class InetAddress : public copyable {
public:
    explicit InetAddress(std::uint16_t port, std::string_view ip = "127.0.0.1");
    InetAddress(const sockaddr* address, socklen_t length);

    [[nodiscard]] static InetAddress anyIPv4(std::uint16_t port = 0);
    [[nodiscard]] static InetAddress loopbackIPv4(std::uint16_t port = 0);
    [[nodiscard]] static InetAddress anyIPv6(std::uint16_t port = 0);
    [[nodiscard]] static InetAddress loopbackIPv6(std::uint16_t port = 0);

    [[nodiscard]] sa_family_t family() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::string ip() const;
    [[nodiscard]] std::string ipPort() const;

    [[nodiscard]] const sockaddr* sockaddrPtr() const noexcept;
    [[nodiscard]] socklen_t sockaddrLength() const noexcept;

private:
    sockaddr_storage address_{};
    socklen_t length_ = 0;
};
