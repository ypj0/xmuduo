#pragma once

#include "noncopyable.h"
#include "EPollPoller.h"
#include <functional>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>

class Channel;
class EPollPoller;

class EventLoop : private noncopyable {
public:
    using Task = std::function<void()>;

    void loop();    // 只能再所属线程调用
    void quit();    // 任意线程调用，请求退出并唤醒循环

    void queueInLoop(Task task);    // 任意线程可调用，始终排队，不立即执行
    void runInLoop(Task task);      // 所述线程立即执行，否则排队

    EventLoop();
    ~EventLoop();

    void runOnce(std::chrono::milliseconds timeout);

    void updateChannel(Channel& channel);
    void removeChannel(Channel& channel);

    [[nodiscard]]
    bool isInLoopThread() const noexcept;

    void assertInLoopThread() const;
    [[nodiscard]] bool isDispatching() const {
        assertInLoopThread();
        return dispatching_;
    }

private:
    class WakeupFd;
    void wakeup();
    void handleWakeup();
    void doPendingTasks();
    void iterate(std::chrono::milliseconds timeout);
    const std::thread::id ownerThread_;
    std::unique_ptr<EPollPoller> poller_;
    std::unique_ptr<WakeupFd> wakeupFd_;    // eventfd
    std::unique_ptr<Channel> wakeupChannel_; // 监听 eventfd的可读事件
    std::vector<EPollPoller::ActiveEvent> activeEvents_;
    bool dispatching_ = false;
    std::mutex taskMutex_;
    std::vector<Task> pendingTasks_;

    std::atomic<bool> quitRequested_{false};    //是否有人请求退出持续循环

    bool looping_ = false;                          // 是否正在执行loop()
    bool runningOnce_ = false;                      // 是否正在遍历本轮 I/O 事件并调用回调

};
