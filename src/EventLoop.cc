#include <stdexcept>
#include <thread>
#include "EventLoop.h"
#include "Channel.h"
#include "EPollPoller.h"

EventLoop::EventLoop()
    : ownerThread_(std::this_thread::get_id()),
      poller_(std::make_unique<EPollPoller>())
{}

EventLoop::~EventLoop() = default;

void EventLoop::runOnce(std::chrono::milliseconds timeout) {
    // 安全检查，防止跨线程调用
    assertInLoopThread();
    // 防止回调里再调用runOnce
    if (dispatching_) { throw std::logic_error("EventLoop dispatch cannot be reentered"); }
    dispatching_ = true;
    try {
        // 等待数据，事件搬运与解包
        poller_->poll(timeout, activeEvents_);
        // 遍历分发
        for (const auto& active : activeEvents_) {
            // Earlier callbacks may have disabled this channel's interests.
            if (active.channel->isReading() || active.channel->isWriting()) {
                active.channel->handleEvent(active.events);
            }
        }
    } catch (...) {
        activeEvents_.clear();
        dispatching_ = false;
        throw;
    }
    activeEvents_.clear();
    dispatching_ = false;
}

void EventLoop::updateChannel(Channel& channel) {
    assertInLoopThread();
    if (&channel.ownerLoop() != this) {
        throw std::logic_error("Channel belongs to another EventLoop");
    }
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel& channel) {
    assertInLoopThread();
    if (dispatching_) { throw std::logic_error("Cannot remove Channel during dispatch"); }
    if (&channel.ownerLoop() != this) {
        throw std::logic_error("Channel belongs to another EventLoop");
    }
    poller_->removeChannel(channel);
}

bool EventLoop::isInLoopThread() const noexcept {
    return ownerThread_ == std::this_thread::get_id();
}

void EventLoop::assertInLoopThread() const {
    if(!isInLoopThread()) {
        throw std::logic_error(
            "EventLoop operation called from another thread");
    }
}
