#include "timestamp.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace xmuduo {
Timestamp::Timestamp()
    : microSecondsSinceEpoch_(0) {}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
    : microSecondsSinceEpoch_(microSecondsSinceEpoch) {}

Timestamp Timestamp::now() {
    using namespace std::chrono;

    const auto now = system_clock::now().time_since_epoch();
    const auto microsecondsSinceEpoch = duration_cast<microseconds>(now).count();
    return Timestamp(microsecondsSinceEpoch);
}

std::string Timestamp::toString() const {
    using namespace std::chrono;

    constexpr int64_t microsecondsPerSecond = 1'000'000;
    const int64_t microsecondPart = microSecondsSinceEpoch_ % microsecondsPerSecond;

    const sys_time<microseconds> timePoint{microseconds{microSecondsSinceEpoch_}};
    const auto dayPoint = floor<days>(timePoint);
    const year_month_day date{dayPoint};
    const hh_mm_ss timeOfDay{floor<microseconds>(timePoint - dayPoint)};

    char buffer[40];
    const int length = std::snprintf(
        buffer, sizeof(buffer), "%04d-%02u-%02u %02lld:%02lld:%02lld.%06lld UTC",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        static_cast<long long>(timeOfDay.hours().count()),
        static_cast<long long>(timeOfDay.minutes().count()),
        static_cast<long long>(timeOfDay.seconds().count()),
        static_cast<long long>(microsecondPart));

    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(buffer)) {
        return {};
    }
    return {buffer, static_cast<std::size_t>(length)};
}

} // namespace xmuduo
