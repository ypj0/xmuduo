#include "InetAddress.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <system_error>

namespace {
socklen_t addressLength(sa_family_t family) {
    switch (family) {
    case AF_INET: return sizeof(sockaddr_in);
    case AF_INET6: return sizeof(sockaddr_in6);
    default: throw std::invalid_argument("InetAddress supports only IPv4 and IPv6");
    }
}

InetAddress makeAddress(sa_family_t family, std::uint16_t port, bool loopback) {
    if (family == AF_INET) {
        return InetAddress{port, loopback ? "127.0.0.1" : "0.0.0.0"};
    }
    return InetAddress{port, loopback ? "::1" : "::"};
}
} // namespace

InetAddress::InetAddress(std::uint16_t port, std::string_view ip) {
    if (ip.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("IP address contains a null character");
    }

    const std::string text{ip};
    auto* ipv4 = reinterpret_cast<sockaddr_in*>(&address_);
    if (::inet_pton(AF_INET, text.c_str(), &ipv4->sin_addr) == 1) {
        ipv4->sin_family = AF_INET;
        ipv4->sin_port = htons(port);
        length_ = sizeof(sockaddr_in);
        return;
    }

    auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&address_);
    if (::inet_pton(AF_INET6, text.c_str(), &ipv6->sin6_addr) == 1) {
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(port);
        length_ = sizeof(sockaddr_in6);
        return;
    }
    throw std::invalid_argument("IP address must be a numeric IPv4 or IPv6 literal: " + text);
}

InetAddress::InetAddress(const sockaddr* address, socklen_t length) {
    if (address == nullptr || length < sizeof(sa_family_t)) {
        throw std::invalid_argument("Socket address is null or missing its family");
    }
    sa_family_t family;
    std::memcpy(&family, address, sizeof(family));
    const auto required = addressLength(family);
    if (length < required || length > sizeof(address_)) {
        throw std::invalid_argument("Invalid socket address length");
    }
    std::memcpy(&address_, address, required);
    length_ = required;
}

InetAddress InetAddress::anyIPv4(std::uint16_t port) {
    return makeAddress(AF_INET, port, false);
}

InetAddress InetAddress::loopbackIPv4(std::uint16_t port) {
    return makeAddress(AF_INET, port, true);
}

InetAddress InetAddress::anyIPv6(std::uint16_t port) {
    return makeAddress(AF_INET6, port, false);
}

InetAddress InetAddress::loopbackIPv6(std::uint16_t port) {
    return makeAddress(AF_INET6, port, true);
}

sa_family_t InetAddress::family() const noexcept {
    return reinterpret_cast<const sockaddr*>(&address_)->sa_family;
}

std::uint16_t InetAddress::port() const noexcept {
    if (family() == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&address_)->sin_port);
    }
    return ntohs(reinterpret_cast<const sockaddr_in6*>(&address_)->sin6_port);
}

std::string InetAddress::ip() const {
    std::array<char, INET6_ADDRSTRLEN> text{};
    const void* source = nullptr;
    if (family() == AF_INET) {
        source = &reinterpret_cast<const sockaddr_in*>(&address_)->sin_addr;
    } else {
        source = &reinterpret_cast<const sockaddr_in6*>(&address_)->sin6_addr;
    }
    if (::inet_ntop(family(), source, text.data(), static_cast<socklen_t>(text.size())) == nullptr) {
        const int error = errno != 0 ? errno : EIO;
        throw std::system_error(error, std::generic_category(), "failed to format IP address");
    }
    return text.data();
}

std::string InetAddress::ipPort() const {
    const auto address = ip();
    if (family() == AF_INET6) {
        return "[" + address + "]:" + std::to_string(port());
    }
    return address + ":" + std::to_string(port());
}

const sockaddr* InetAddress::sockaddrPtr() const noexcept {
    return reinterpret_cast<const sockaddr*>(&address_);
}

socklen_t InetAddress::sockaddrLength() const noexcept {
    return length_;
}
