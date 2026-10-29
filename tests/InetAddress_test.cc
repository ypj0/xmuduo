#include "InetAddress.h"
#include "TestSupport.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <type_traits>

static_assert(std::is_copy_constructible_v<InetAddress>);
static_assert(std::is_copy_assignable_v<InetAddress>);

int main() {
    return runTests([] {
        const InetAddress ipv4{8080, "192.0.2.17"};
        CHECK(ipv4.family() == AF_INET);
        CHECK(ipv4.port() == 8080);
        CHECK(ipv4.ip() == "192.0.2.17");
        CHECK(ipv4.ipPort() == "192.0.2.17:8080");
        CHECK(ipv4.sockaddrLength() == sizeof(sockaddr_in));
        CHECK(reinterpret_cast<const sockaddr_in*>(ipv4.sockaddrPtr())->sin_port == htons(8080));

        const InetAddress ipv6{443, "2001:db8::1"};
        CHECK(ipv6.family() == AF_INET6);
        CHECK(ipv6.port() == 443);
        CHECK(ipv6.ip() == "2001:db8::1");
        CHECK(ipv6.ipPort() == "[2001:db8::1]:443");
        CHECK(ipv6.sockaddrLength() == sizeof(sockaddr_in6));
        CHECK(reinterpret_cast<const sockaddr_in6*>(ipv6.sockaddrPtr())->sin6_port == htons(443));

        CHECK(InetAddress::anyIPv4().ipPort() == "0.0.0.0:0");
        CHECK(InetAddress::loopbackIPv4(80).ipPort() == "127.0.0.1:80");
        CHECK(InetAddress::anyIPv6(9).ipPort() == "[::]:9");
        CHECK(InetAddress::loopbackIPv6().ipPort() == "[::1]:0");

        const InetAddress copied{ipv6};
        CHECK(copied.ipPort() == ipv6.ipPort());
        const InetAddress fromSockaddr{ipv4.sockaddrPtr(), ipv4.sockaddrLength()};
        CHECK(fromSockaddr.ipPort() == ipv4.ipPort());
        sockaddr_in6 scoped{};
        scoped.sin6_family = AF_INET6;
        scoped.sin6_port = htons(12);
        scoped.sin6_scope_id = 7;
        CHECK(::inet_pton(AF_INET6, "fe80::1", &scoped.sin6_addr) == 1);
        const InetAddress scopedAddress{reinterpret_cast<const sockaddr*>(&scoped),
                                        sizeof(scoped)};
        CHECK(reinterpret_cast<const sockaddr_in6*>(scopedAddress.sockaddrPtr())->sin6_scope_id == 7);
        expectThrows<std::invalid_argument>([] { InetAddress bad{123, "localhost"}; });
        expectThrows<std::invalid_argument>([] {
            using namespace std::string_view_literals;
            InetAddress bad{123, "127.0.0.1\0ignored"sv};
        });
        expectThrows<std::invalid_argument>([] {
            InetAddress bad{nullptr, static_cast<socklen_t>(sizeof(sockaddr_in))};
        });
        expectThrows<std::invalid_argument>([&] {
            InetAddress bad{ipv4.sockaddrPtr(), static_cast<socklen_t>(sizeof(sa_family_t))};
        });
        expectThrows<std::invalid_argument>([&] {
            InetAddress bad{ipv4.sockaddrPtr(), 0};
        });
        sockaddr unsupported{};
        unsupported.sa_family = AF_UNIX;
        expectThrows<std::invalid_argument>([&] {
            InetAddress bad{&unsupported, sizeof(unsupported)};
        });
    });
}
