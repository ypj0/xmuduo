#include "LogBuffer.h"
#include "TestSupport.h"

#include <limits>

int main() {
    return runTests([] {
        LogBuffer<8> small;
        CHECK(small.empty());
        CHECK(small.capacity() == 8);
        CHECK(small.available() == 8);
        CHECK(small.view().empty());
        CHECK(small.append(std::string_view{}));
        CHECK(small.append("abc"));
        CHECK(small.size() == 3);
        CHECK(small.available() == 5);
        CHECK(!small.append("123456"));
        CHECK(small.view() == "abc");
        CHECK(small.append("12345"));
        CHECK(small.view() == "abc12345");
        CHECK(small.available() == 0);
        CHECK(small.append(""));
        CHECK(!small.append("x"));
        small.reset();
        CHECK(small.empty());
        CHECK(small.available() == 8);
        CHECK(small.append(std::string_view{"a\0b", 3}));
        CHECK(small.view() == std::string_view{"a\0b", 3});
        small.reset();
        CHECK(small.append("abc"));
        CHECK(small.append(small.view()));
        CHECK(small.view() == "abcabc");

        LogBuffer<> buffer;
        CHECK(buffer.append("request: "));
        int fd = 42; // Also exercise forwarding an lvalue.
        CHECK(buffer.appendFormat("fd={}, elapsed={:.2f}ms, hex={:04x}", fd, 1.25, 15));
        CHECK(buffer.view() == "request: fd=42, elapsed=1.25ms, hex=000f");
        buffer.reset();
        CHECK(buffer.appendFormat("{} {}", std::numeric_limits<long long>::min(),
                                  std::numeric_limits<unsigned long long>::max()));
        CHECK(buffer.view() == "-9223372036854775808 18446744073709551615");
        buffer.reset();
        CHECK(buffer.appendFormat("{{}} {} {}", true, std::string_view{"a\0b", 3}));
        CHECK(buffer.view() == std::string_view{"{} true a\0b", 11});

        small.reset();
        CHECK(small.append("abc"));
        CHECK(!small.appendFormat("{:06}", 42));
        CHECK(small.view() == "abc");
        CHECK(small.size() == 3);
        CHECK(small.available() == 5);
        CHECK(small.appendFormat("{:05}", 42));
        CHECK(small.view() == "abc00042");
        CHECK(small.appendFormat(""));
        CHECK(!small.appendFormat("{}", 1));
        CHECK(small.view() == "abc00042");

        small.reset();
        CHECK(small.append("ok"));
        expectThrows<std::format_error>([&] {
            (void)small.appendFormat("x{:{}}", 42, -1);
        });
        CHECK(small.view() == "ok");
        CHECK(small.size() == 2);
        CHECK(small.appendFormat("-{}", 7));
        CHECK(small.view() == "ok-7");
    });
}
