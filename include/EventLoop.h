#pragma once

#include "noncopyable.h"
#include "EPollPoller.h"
#include <thread>
#include <memory>

class Channel;
class EPollPoller;

class EventLoop : private noncopyable {
public:
    EventLoop();
    ~EventLoop();
    void runOnce(std::chrono::milliseconds timeout);

    void updateChannel(Channel& channel);
    void removeChannel(Channel& channel);

    [[nodiscard]]
    bool isInLoopThread() const noexcept;

    void assertInLoopThread() const;
private:
    const std::thread::id ownerThread_;
    std::unique_ptr<EPollPoller> poller_;
    std::vector<EPollPoller::ActiveEvent> activeEvents_;
    bool dispatching_ = false;

};
