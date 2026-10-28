#include "Timestamp.h"
#include "TimeFormatting.h"

#include <cstdio>
#include <stdexcept>

namespace time_detail {
void checkCalendarRange(Timestamp::Duration sinceEpoch) {
    if (sinceEpoch < minTime.time_since_epoch() ||
        sinceEpoch >= endTime.time_since_epoch()) {
        throw std::out_of_range("Time must be in calendar years 0001 through 9999");
    }
}

std::string formatCalendar(Timestamp::Duration sinceEpoch) {
    using namespace std::chrono;
    checkCalendarRange(sinceEpoch);
    const Timestamp::TimePoint time{sinceEpoch};
    const auto date = floor<days>(time);
    const year_month_day ymd{date};
    const hh_mm_ss tod{time - date};
    char buffer[32];
    const auto size = std::snprintf(buffer, sizeof(buffer),
        "%04d-%02u-%02uT%02lld:%02lld:%02lld.%06lld",
        int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()),
        static_cast<long long>(tod.hours().count()),
        static_cast<long long>(tod.minutes().count()),
        static_cast<long long>(tod.seconds().count()),
        static_cast<long long>(tod.subseconds().count()));
    if (size < 0 || static_cast<std::size_t>(size) >= sizeof(buffer)) {
        throw std::runtime_error("Failed to format timestamp");
    }
    return {buffer, static_cast<std::size_t>(size)};
}
} // namespace time_detail

Timestamp::Timestamp(TimePoint time) : time_(time) {
    time_detail::checkCalendarRange(time.time_since_epoch());
}

Timestamp Timestamp::now() {
    return Timestamp{std::chrono::floor<Duration>(std::chrono::system_clock::now())};
}

std::string Timestamp::toUtcString() const {
    return time_detail::formatCalendar(timeSinceEpoch()) + 'Z';
}

Timestamp Timestamp::operator+(Duration duration) const {
    // Compare before adding, so even duration::min/max cannot overflow.
    if (duration < time_detail::minTime - time_ || duration >= time_detail::endTime - time_) {
        throw std::out_of_range("Timestamp addition exceeds supported range");
    }
    return Timestamp{time_ + duration};
}

Timestamp Timestamp::operator-(Duration duration) const {
    // Do not negate duration: -duration::min() is undefined.
    if (duration > time_ - time_detail::minTime || duration <= time_ - time_detail::endTime) {
        throw std::out_of_range("Timestamp subtraction exceeds supported range");
    }
    return Timestamp{time_ - duration};
}
