#pragma once

#include "Timestamp.h"

namespace time_detail {
inline constexpr Timestamp::TimePoint minTime{
    std::chrono::sys_days{std::chrono::year{1}/1/1}};
inline constexpr Timestamp::TimePoint endTime{
    std::chrono::sys_days{std::chrono::year{10000}/1/1}};

void checkCalendarRange(Timestamp::Duration sinceEpoch);
std::string formatCalendar(Timestamp::Duration sinceEpoch);
} // namespace time_detail
