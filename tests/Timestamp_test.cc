#include "Timestamp.h"
#include "TestSupport.h"

#include <type_traits>

using namespace std::chrono;
static_assert(std::is_trivially_copyable_v<Timestamp>);
static_assert(sizeof(Timestamp) == sizeof(Timestamp::TimePoint));
static_assert(!std::is_convertible_v<int, Timestamp>);

int main() {
    return runTests([] {
        const Timestamp epoch;
        CHECK(epoch.timeSinceEpoch() == 0us);
        CHECK(epoch.toUtcString() == "1970-01-01T00:00:00.000000Z");
        CHECK((epoch - 1us).toUtcString() == "1969-12-31T23:59:59.999999Z");
        CHECK((epoch + 123456us).toUtcString() == "1970-01-01T00:00:00.123456Z");
        const Timestamp leap{sys_days{2024y/February/29} + 23h + 59min + 59s + 999999us};
        CHECK(leap.toUtcString() == "2024-02-29T23:59:59.999999Z");
        CHECK((leap + 1us).toUtcString() == "2024-03-01T00:00:00.000000Z");
        CHECK((Timestamp{sys_days{1999y/December/31}} + 24h).toUtcString() ==
              "2000-01-01T00:00:00.000000Z");
        CHECK(epoch < leap);
        CHECK(epoch == Timestamp{Timestamp::TimePoint{}});
        CHECK((epoch + 3s) - epoch == 3s);
        CHECK((epoch - (-3s)) == epoch + 3s);
        CHECK((epoch + (-3s)) == epoch - 3s);

        const Timestamp first{sys_days{1y/January/1}};
        const Timestamp last{sys_days{10000y/January/1} - 1us};
        CHECK(first.toUtcString() == "0001-01-01T00:00:00.000000Z");
        CHECK(last.toUtcString() == "9999-12-31T23:59:59.999999Z");
        CHECK(first + (last - first) == last);
        CHECK(last - (last - first) == first);
        expectThrows<std::out_of_range>([&] { (void)(first - 1us); });
        expectThrows<std::out_of_range>([&] { (void)(last + 1us); });
        for (auto extreme : {Timestamp::Duration::min(), Timestamp::Duration::max()}) {
            expectThrows<std::out_of_range>([&] { (void)Timestamp{Timestamp::TimePoint{extreme}}; });
            expectThrows<std::out_of_range>([&] { (void)(epoch + extreme); });
            expectThrows<std::out_of_range>([&] { (void)(epoch - extreme); });
        }
        const auto before = floor<microseconds>(system_clock::now());
        const auto now = Timestamp::now();
        const auto after = floor<microseconds>(system_clock::now());
        CHECK(now.timePoint() >= before);
        CHECK(now.timePoint() <= after);
    });
}
