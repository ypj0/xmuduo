#pragma once

#include "RollingFileSink.h"
#include "noncopyable.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

// Bounded asynchronous adapter that copies each record before returning.
class AsyncLogSink : noncopyable {
public:
    AsyncLogSink(std::filesystem::path directory,
                 std::string basename,
                 std::uintmax_t rollSize,
                 std::size_t queueCapacityBytes = 1024 * 1024,
                 std::chrono::milliseconds flushInterval = std::chrono::seconds{3});
    ~AsyncLogSink();

    void append(std::string_view message);
    void flush();
    void stop(); // drains accepted records, flushes, joins; may report I/O errors

private:
    struct PendingMessage {
        std::uint64_t sequence;
        std::size_t charge;
        std::string bytes;
    };

    struct FlushRequest {
        std::uint64_t ticket;
        std::uint64_t sequence;
    };

    void workerLoop(std::stop_token stopToken) noexcept;
    void recordWorkerError(std::exception_ptr error) noexcept;
    void rethrowWorkerError() const;

    const std::size_t queueCapacityBytes_;
    const std::chrono::milliseconds flushInterval_;
    std::unique_ptr<RollingFileSink> sink_;

    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable capacityAvailable_;
    std::condition_variable stateChanged_;
    std::deque<PendingMessage> queue_;
    std::deque<FlushRequest> flushRequests_;
    std::size_t outstandingBytes_ = 0; // queued plus currently-written messages
    std::uint64_t submittedSequence_ = 0;
    std::uint64_t completedSequence_ = 0;
    std::uint64_t requestedFlush_ = 0;
    std::uint64_t completedFlush_ = 0;
    std::exception_ptr workerError_;
    bool stopping_ = false;
    std::mutex stopMutex_; // serializes concurrent stop()/join() calls
    std::jthread worker_; // declared last; joined before other state is destroyed
};
