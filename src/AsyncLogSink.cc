#include "AsyncLogSink.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
std::size_t checkedCapacity(std::size_t capacity, std::size_t overhead) {
    if (capacity < overhead) {
        throw std::invalid_argument("Async log queue capacity is too small");
    }
    return capacity;
}

std::chrono::milliseconds checkedInterval(std::chrono::milliseconds interval) {
    if (interval < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Flush interval cannot be negative");
    }
    return interval;
}
} // namespace

AsyncLogSink::AsyncLogSink(std::filesystem::path directory,
                           std::string basename,
                           std::uintmax_t rollSize,
                           std::size_t queueCapacityBytes,
                           std::chrono::milliseconds flushInterval)
    : queueCapacityBytes_(checkedCapacity(queueCapacityBytes, sizeof(PendingMessage))),
      flushInterval_(checkedInterval(flushInterval)),
      sink_(std::make_unique<RollingFileSink>(
          std::move(directory), std::move(basename), rollSize,
          std::chrono::milliseconds::zero())) {
    worker_ = std::jthread([this](std::stop_token stopToken) {
        workerLoop(stopToken);
    });
}

AsyncLogSink::~AsyncLogSink() {
    try { stop(); } catch (...) { /* Explicit stop()/flush() reports errors. */ }
}

void AsyncLogSink::stop() {
    std::lock_guard stopLock{stopMutex_};
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    worker_.request_stop();
    workAvailable_.notify_all();
    capacityAvailable_.notify_all();
    if (worker_.joinable()) { worker_.join(); }
    std::lock_guard lock{mutex_};
    rethrowWorkerError();
}

void AsyncLogSink::append(std::string_view message) {
    std::unique_lock lock{mutex_};
    rethrowWorkerError();
    if (stopping_) { throw std::runtime_error("Async log sink is stopping"); }
    if (message.empty()) { return; }
    if (message.size() > queueCapacityBytes_ - sizeof(PendingMessage)) {
        throw std::length_error("Log record exceeds async queue capacity");
    }
    const auto charge = sizeof(PendingMessage) + message.size();

    capacityAvailable_.wait(lock, [this, charge] {
        return workerError_ || stopping_ || outstandingBytes_ <= queueCapacityBytes_ - charge;
    });
    rethrowWorkerError();
    if (stopping_) { throw std::runtime_error("Async log sink is stopping"); }
    if (submittedSequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Async log sequence exhausted");
    }

    const auto sequence = submittedSequence_ + 1;
    // Copy only after capacity is available, so blocked producer calls do not
    // hold extra unaccounted message copies outside the bounded queue.
    queue_.push_back(PendingMessage{sequence, charge, std::string{message}});
    submittedSequence_ = sequence;
    outstandingBytes_ += charge;
    lock.unlock();
    workAvailable_.notify_one();
}

void AsyncLogSink::flush() {
    std::unique_lock lock{mutex_};
    rethrowWorkerError();
    if (stopping_) { throw std::runtime_error("Async log sink is stopping"); }
    if (requestedFlush_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Async flush ticket exhausted");
    }
    const auto ticket = requestedFlush_ + 1;
    flushRequests_.push_back(FlushRequest{ticket, submittedSequence_});
    requestedFlush_ = ticket;
    workAvailable_.notify_one();
    stateChanged_.wait(lock, [this, ticket] {
        return workerError_ || completedFlush_ >= ticket;
    });
    rethrowWorkerError();
}

void AsyncLogSink::workerLoop(std::stop_token stopToken) noexcept {
    try {
        auto lastFlush = std::chrono::steady_clock::now();
        for (;;) {
            std::deque<PendingMessage> batch;
            std::optional<FlushRequest> flushRequest;
            bool shouldStop = false;
            {
                std::unique_lock lock{mutex_};
                const auto ready = [this, &stopToken] {
                    return !queue_.empty() || !flushRequests_.empty() || stopping_ ||
                           stopToken.stop_requested();
                };
                if (flushInterval_ == std::chrono::milliseconds::zero()) {
                    workAvailable_.wait(lock, ready);
                } else {
                    // Clamp each wait to avoid overflowing the clock conversion.
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - lastFlush);
                    if (elapsed < flushInterval_) {
                        workAvailable_.wait_for(lock, std::min(flushInterval_ - elapsed,
                            std::chrono::milliseconds{60000}), ready);
                    }
                }

                if (!flushRequests_.empty() &&
                    flushRequests_.front().sequence <= completedSequence_) {
                    flushRequest = flushRequests_.front();
                    flushRequests_.pop_front();
                } else if (!queue_.empty()) {
                    if (flushRequests_.empty()) {
                        batch.swap(queue_);
                    } else {
                        const auto target = flushRequests_.front().sequence;
                        while (!queue_.empty() && queue_.front().sequence <= target) {
                            batch.push_back(std::move(queue_.front()));
                            queue_.pop_front();
                        }
                    }
                } else if (stopping_ || stopToken.stop_requested()) {
                    shouldStop = true;
                }
            }

            if (!batch.empty()) {
                std::size_t completedBytes = 0;
                for (const auto& message : batch) {
                    sink_->append(message.bytes);
                    completedBytes += message.charge;
                }
                const auto completed = batch.back().sequence;
                batch.clear(); // release record storage before returning capacity
                {
                    std::lock_guard lock{mutex_};
                    completedSequence_ = completed;
                    outstandingBytes_ -= completedBytes;
                }
                capacityAvailable_.notify_all();
                stateChanged_.notify_all();
            }

            if (flushRequest) {
                sink_->flush();
                {
                    std::lock_guard lock{mutex_};
                    completedFlush_ = flushRequest->ticket;
                }
                stateChanged_.notify_all();
                lastFlush = std::chrono::steady_clock::now();
            }

            if (shouldStop) {
                sink_->flush();
                return;
            }
            if (flushInterval_ > std::chrono::milliseconds::zero() &&
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - lastFlush) >= flushInterval_) {
                sink_->flush();
                lastFlush = std::chrono::steady_clock::now();
            }
        }
    } catch (...) {
        recordWorkerError(std::current_exception());
    }
}

void AsyncLogSink::recordWorkerError(std::exception_ptr error) noexcept {
    {
        std::lock_guard lock{mutex_};
        if (!workerError_) { workerError_ = std::move(error); }
        stopping_ = true;
        queue_.clear();
        flushRequests_.clear();
        outstandingBytes_ = 0;
    }
    capacityAvailable_.notify_all();
    stateChanged_.notify_all();
    workAvailable_.notify_all();
}

void AsyncLogSink::rethrowWorkerError() const {
    if (workerError_) { std::rethrow_exception(workerError_); }
}
