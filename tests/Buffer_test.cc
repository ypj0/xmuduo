#include "Buffer.h"
#include "TestSupport.h"
#include <algorithm>
#include <limits>
#include <random>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<Buffer>);
static_assert(!std::is_move_constructible_v<Buffer>);

int main() {
    return runTests([] {
        using namespace std::string_view_literals;
        Buffer buffer{8, 32};
        CHECK(buffer.readableView().empty());
        buffer.append("abcdef");
        buffer.retrieve(4);
        buffer.append("12345"); // Compact: two bytes survive, capacity unchanged.
        CHECK(buffer.readableView() == "ef12345");
        CHECK(buffer.capacity() == 8);
        buffer.append("6789");
        CHECK(buffer.readableView() == "ef123456789");
        CHECK(buffer.capacity() == 16);
        CHECK(buffer.retrieveAsString(2) == "ef");
        buffer.retrieveAll();
        CHECK(buffer.writableBytes() == 16);
        buffer.append("a\0b"sv);
        CHECK(buffer.retrieveAsString(3) == std::string("a\0b", 3));
        CHECK(buffer.retrieveAsString(0).empty());
        buffer.append({});

        auto writable = buffer.writableSpan(5);
        std::copy_n("hello", 5, writable.begin());
        CHECK(buffer.readableBytes() == 0); // Writing memory does not commit it.
        buffer.commitWritten(5);
        CHECK(buffer.readableView() == "hello");
        expectThrows<std::out_of_range>([&] { buffer.commitWritten(buffer.writableBytes() + 1); });
        expectThrows<std::out_of_range>([&] { buffer.retrieve(6); });
        expectThrows<std::out_of_range>([&] { (void)buffer.retrieveAsString(6); });
        expectThrows<std::length_error>([&] { buffer.append(std::string(28, 'x')); });
        expectThrows<std::length_error>([&] { buffer.ensureWritable(std::numeric_limits<std::size_t>::max()); });
        CHECK(buffer.readableView() == "hello");
        buffer.append(std::string(27, 'x')); // Exact maximum.
        CHECK(buffer.readableBytes() == 32 && buffer.capacity() == 32);
        CHECK(buffer.writableSpan(0).empty());
        buffer.retrieveAll();
        buffer.append("reused");
        CHECK(buffer.readableView() == "reused");

        Buffer alias{8, 64};
        alias.append("abcdef");
        alias.retrieve(3);
        alias.append(alias.readableView()); // Self-append across compaction.
        CHECK(alias.readableView() == "defdef");
        alias.append(alias.readableView()); // Self-append across growth.
        CHECK(alias.readableView() == "defdefdefdef");
        alias.append(alias.readableView().substr(2, 2));
        CHECK(alias.readableView() == "defdefdefdeffd");

        Buffer empty{0, 4};
        CHECK(empty.writableSpan(0).empty());
        empty.commitWritten(0);
        empty.retrieve(0);
        empty.append("1234");
        CHECK(empty.readableView() == "1234");
        expectThrows<std::invalid_argument>([] { Buffer invalid{5, 4}; });
        expectThrows<std::invalid_argument>([] { Buffer invalid{0, 0}; });

        // Compare thousands of mixed operations against a simple string model.
        Buffer mixed{0, 128};
        std::string model;
        std::mt19937 random{12345};
        for (int step = 0; step < 5000; ++step) {
            const auto count = static_cast<std::size_t>(random() % 20);
            if (random() % 2 == 0) {
                std::string bytes(count, static_cast<char>(random() % 256));
                if (count <= 128 - model.size()) { mixed.append(bytes); model += bytes; }
                else { expectThrows<std::length_error>([&] { mixed.append(bytes); }); }
            } else {
                const auto consumed = std::min(count, model.size());
                CHECK(mixed.retrieveAsString(consumed) == model.substr(0, consumed));
                model.erase(0, consumed);
            }
            CHECK(mixed.readableView() == model);
            CHECK(mixed.capacity() <= 128);
            CHECK(mixed.readableBytes() + mixed.writableBytes() <= mixed.capacity());
        }
    });
}
