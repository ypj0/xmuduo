#include "TimeZone.h"
#include "TimeFormatting.h"

#include <cstdio>
#include <stdexcept>


TimeZone::TimeZone(std::string_view name) : zone_(std::chrono::locate_zone(name)) {}
TimeZone TimeZone::utc() { return TimeZone{"UTC"}; }
std::string_view TimeZone::name() const noexcept { return zone_->name(); }

TimeZone::LocalTime TimeZone::toLocal(Timestamp timestamp) const {
    const auto local = zone_->to_local(timestamp.timePoint());
    time_detail::checkCalendarRange(local.time_since_epoch());
    return local;
}

Timestamp TimeZone::toTimestamp(LocalTime local, AmbiguousTime choice) const {
    time_detail::checkCalendarRange(local.time_since_epoch());
    if (choice != AmbiguousTime::reject && choice != AmbiguousTime::earliest &&
        choice != AmbiguousTime::latest) {
        throw std::invalid_argument("Invalid ambiguous-time choice");
    }
    const auto result = zone_->get_info(local);
    if (result.result == std::chrono::local_info::nonexistent) {
        throw std::chrono::nonexistent_local_time{local, result};
    }
    if (choice == AmbiguousTime::reject) {
        return Timestamp{zone_->to_sys(local)};
    }
    return Timestamp{zone_->to_sys(local, choice == AmbiguousTime::earliest
        ? std::chrono::choose::earliest : std::chrono::choose::latest)};
}

std::chrono::sys_info TimeZone::info(Timestamp timestamp) const {
    return zone_->get_info(timestamp.timePoint());
}

std::string formatTimestamp(Timestamp timestamp, const TimeZone& zone) {
    const auto local = zone.toLocal(timestamp);
    const auto offset = zone.info(timestamp).offset.count();
    const auto absolute = offset < 0 ? -offset : offset;
    char buffer[32];
    // Preserve second-resolution offsets found in historical IANA rules.
    const auto size = std::snprintf(buffer, sizeof(buffer), "%c%02lld:%02lld:%02lld",
        offset < 0 ? '-' : '+', static_cast<long long>(absolute / 3600),
        static_cast<long long>((absolute / 60) % 60),
        static_cast<long long>(absolute % 60));
    if (size < 0 || static_cast<std::size_t>(size) >= sizeof(buffer)) {
        throw std::runtime_error("Failed to format time-zone offset");
    }
    return time_detail::formatCalendar(local.time_since_epoch()) + buffer + '[' +
        std::string{zone.name()} + ']';
}
