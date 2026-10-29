#include "RollingFileSink.h"

#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

RollingFileSink::RollingFileSink(std::filesystem::path directory,
                                 std::string basename,
                                 std::uintmax_t rollSize,
                                 std::chrono::milliseconds flushInterval)
    : directory_(std::move(directory)), basename_(std::move(basename)),
      rollSize_(rollSize), flushInterval_(flushInterval) {
    if (rollSize_ == 0) {
        throw std::invalid_argument("Log roll size must be positive");
    }
    if (flushInterval_ < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Flush interval cannot be negative");
    }
    if (basename_.empty() || basename_ == "." || basename_ == ".." ||
        basename_.find('/') != std::string::npos ||
        basename_.find('\0') != std::string::npos) {
        throw std::invalid_argument("Log basename must be a single filename component");
    }
    roll();
    if (flushInterval_ > std::chrono::milliseconds::zero()) {
        flushThread_ = std::jthread([this](std::stop_token stopToken) {
            periodicFlush(stopToken);
        });
    }
}

void RollingFileSink::append(std::string_view message) {
    std::lock_guard lock{mutex_};
    rethrowBackgroundError();
    if (message.empty()) { return; }
    // Subtraction avoids overflow; oversized records occupy a file on their own.
    if (writtenBytes_ != 0 &&
        (writtenBytes_ >= rollSize_ || message.size() > rollSize_ - writtenBytes_)) {
        roll();
    }
    currentFile_->append(message);
    writtenBytes_ += message.size();
}

void RollingFileSink::flush() {
    std::lock_guard lock{mutex_};
    rethrowBackgroundError();
    currentFile_->flush();
}

void RollingFileSink::periodicFlush(std::stop_token stopToken) {
    std::unique_lock lock{mutex_};
    while (!stopToken.stop_requested()) {
        (void)flushCondition_.wait_for(lock, stopToken, flushInterval_, [] {
            return false;
        });
        if (stopToken.stop_requested()) { return; }
        try {
            currentFile_->flush();
        } catch (...) {
            backgroundError_ = std::current_exception();
            return;
        }
    }
}

void RollingFileSink::rethrowBackgroundError() const {
    if (backgroundError_) { std::rethrow_exception(backgroundError_); }
}

void RollingFileSink::roll() {
    // Report buffered write failures before releasing the old stream.
    if (currentFile_) { currentFile_->flush(); }
    for (;;) {
        if (sequence_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Log file sequence exhausted");
        }
        const auto path = directory_ /
            (basename_ + "." + std::to_string(sequence_++) + ".log");
        std::unique_ptr<FileSink> next;
        try {
            next = std::make_unique<FileSink>(path, FileSink::OpenMode::createExclusive);
        } catch (const std::system_error& error) {
            if (error.code() == std::errc::file_exists) { continue; }
            throw;
        }
        currentFile_ = std::move(next);
        writtenBytes_ = 0;
        return;
    }
}
