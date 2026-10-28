#include "TimeZone.h"
#include "TestSupport.h"

#include <future>
#include <vector>

using namespace std::chrono;

int main() {
    return runTests([] {
        const auto utc = TimeZone::utc();
        CHECK(utc.name() == locate_zone("UTC")->name());
        CHECK(utc.toLocal(Timestamp{}) == TimeZone::LocalTime{});
        CHECK(utc.toTimestamp(TimeZone::LocalTime{}) == Timestamp{});
        CHECK(formatTimestamp(Timestamp{}, utc) ==
              "1970-01-01T00:00:00.000000+00:00:00[" + std::string{utc.name()} + "]");
        expectThrows<std::runtime_error>([] { (void)TimeZone{"Invalid/NoSuchZone"}; });
        expectThrows<std::runtime_error>([] { (void)TimeZone{""}; });

        const TimeZone shanghai{"Asia/Shanghai"};
        const Timestamp instant{sys_days{2024y/January/1} + 20h + 123456us};
        CHECK(shanghai.info(instant).offset == 8h);
        CHECK(formatTimestamp(instant, shanghai) ==
              "2024-01-02T04:00:00.123456+08:00:00[Asia/Shanghai]");
        CHECK(shanghai.toTimestamp(shanghai.toLocal(instant)) == instant);
        CHECK(utc.toTimestamp(utc.toLocal(Timestamp{} - 1us)) == Timestamp{} - 1us);
        const TimeZone kathmandu{"Asia/Kathmandu"};
        CHECK(kathmandu.info(instant).offset == 5h + 45min);
        CHECK(formatTimestamp(instant, kathmandu) ==
              "2024-01-02T01:45:00.123456+05:45:00[Asia/Kathmandu]");

        const TimeZone ny{"America/New_York"};
        CHECK(ny.info(Timestamp{sys_days{2024y/January/1}}).offset == -5h);
        CHECK(ny.info(Timestamp{sys_days{2024y/July/1}}).offset == -4h);
        const Timestamp spring{sys_days{2024y/March/10} + 7h};
        CHECK(formatTimestamp(spring - 1us, ny) ==
              "2024-03-10T01:59:59.999999-05:00:00[America/New_York]");
        CHECK(formatTimestamp(spring, ny) ==
              "2024-03-10T03:00:00.000000-04:00:00[America/New_York]");
        const auto gap = local_days{2024y/March/10} + 2h + 30min;
        for (const auto policy : {TimeZone::AmbiguousTime::reject,
                                 TimeZone::AmbiguousTime::earliest,
                                 TimeZone::AmbiguousTime::latest}) {
            expectThrows<nonexistent_local_time>([&] { (void)ny.toTimestamp(gap, policy); });
        }
        const auto repeated = local_days{2024y/November/3} + 1h + 30min + 123456us;
        expectThrows<ambiguous_local_time>([&] { (void)ny.toTimestamp(repeated); });
        const auto early = ny.toTimestamp(repeated, TimeZone::AmbiguousTime::earliest);
        const auto late = ny.toTimestamp(repeated, TimeZone::AmbiguousTime::latest);
        CHECK(early == Timestamp{sys_days{2024y/November/3} + 5h + 30min + 123456us});
        CHECK(late - early == 1h);
        CHECK(ny.toLocal(early) == repeated);
        CHECK(ny.toLocal(late) == repeated);
        CHECK(ny.info(early).offset == -4h);
        CHECK(ny.info(late).offset == -5h);
        CHECK(formatTimestamp(early, ny) != formatTimestamp(late, ny));
        const Timestamp historical{sys_days{1880y/January/1}};
        const TimeZone paris{"Europe/Paris"};
        CHECK(paris.info(historical).offset == 9min + 21s);
        CHECK(formatTimestamp(historical, paris) ==
              "1880-01-01T00:09:21.000000+00:09:21[Europe/Paris]");

        expectThrows<std::out_of_range>([&] {
            (void)utc.toTimestamp(TimeZone::LocalTime{Timestamp::Duration::max()});
        });
        expectThrows<std::out_of_range>([&] {
            (void)shanghai.toLocal(Timestamp{sys_days{10000y/January/1} - 1us});
        });
        expectThrows<std::out_of_range>([&] {
            (void)shanghai.toTimestamp(local_days{1y/January/1});
        });
        expectThrows<std::invalid_argument>([&] {
            (void)utc.toTimestamp({}, static_cast<TimeZone::AmbiguousTime>(99));
        });
        std::vector<std::future<void>> workers;
        for (int worker = 0; worker < 4; ++worker) {
            workers.push_back(std::async(std::launch::async, [&] {
                for (int i = 0; i < 100; ++i) {
                    CHECK(shanghai.toTimestamp(shanghai.toLocal(instant)) == instant);
                    CHECK(formatTimestamp(spring, ny) ==
                          "2024-03-10T03:00:00.000000-04:00:00[America/New_York]");
                }
            }));
        }
        for (auto& worker : workers) { worker.get(); }
    });
}
