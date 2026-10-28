#pragma once

#include "Timestamp.h"
#include <string_view>


// Immutable handle into the standard library's IANA time-zone database.
// The referenced tzdb entry must not be erased while this handle is in use.
class TimeZone {
public:
    using LocalTime = std::chrono::local_time<Timestamp::Duration>;
    enum class AmbiguousTime { reject, earliest, latest };

    explicit TimeZone(std::string_view name);
    [[nodiscard]] static TimeZone utc();
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] LocalTime toLocal(Timestamp timestamp) const;
    // Nonexistent local times always throw std::chrono::nonexistent_local_time.
    // Repeated local times throw ambiguous_local_time unless a choice is given.
    [[nodiscard]] Timestamp toTimestamp(
        LocalTime local, AmbiguousTime choice = AmbiguousTime::reject) const;
    [[nodiscard]] std::chrono::sys_info info(Timestamp timestamp) const;

private:
    const std::chrono::time_zone* zone_;
};

// Local calendar time + numeric offset + IANA name; no process-global TZ state.
[[nodiscard]] std::string formatTimestamp(Timestamp timestamp, const TimeZone& zone);

