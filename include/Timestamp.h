#pragma once

#include <chrono>
#include <compare>
#include <string>


// A wall-clock instant, with microsecond precision. Not a timer clock.
// Supported UTC range: [0001-01-01, 10000-01-01).
class Timestamp {
public:
    using Duration = std::chrono::microseconds;
    using TimePoint = std::chrono::sys_time<Duration>;

    constexpr Timestamp() noexcept = default; // Unix epoch, not an invalid value.
    explicit Timestamp(TimePoint time);

    [[nodiscard]] static Timestamp now();
    [[nodiscard]] constexpr TimePoint timePoint() const noexcept { return time_; }
    [[nodiscard]] constexpr Duration timeSinceEpoch() const noexcept {
        return time_.time_since_epoch();
    }
    [[nodiscard]] std::string toUtcString() const;

    auto operator<=>(const Timestamp&) const = default;
    [[nodiscard]] Timestamp operator+(Duration duration) const;
    [[nodiscard]] Timestamp operator-(Duration duration) const;
    [[nodiscard]] constexpr Duration operator-(Timestamp other) const noexcept {
        return time_ - other.time_;
    }

private:
    TimePoint time_{};
};

