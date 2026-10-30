#include <stdexcept>
#include <thread>
#include "EventLoop.h"
#include "Channel.h"
#include "EPollPoller.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>
#include <exception>
#include <cassert>
#include <limits>

class EventLoop::WakeupFd : noncopyable {
public:
    WakeupFd() : fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
        if (fd < 0) { throw std::system_error(errno, std::generic_category(), "eventfd"); }
    }
    ~WakeupFd() {
        const int saved = errno;
        (void)::close(fd);
        errno = saved;
    }
    int fd;
};

EventLoop::EventLoop()
    : ownerThread_(std::this_thread::get_id()), // 记住当前所属线程
      poller_(std::make_unique<EPollPoller>()), // 创建epoll后端
      wakeupFd_(std::make_unique<WakeupFd>()),  // 创建eventfd
      wakeupChannel_(std::make_unique<Channel>(*this, wakeupFd_->fd)) { //建立eventfd对应的channel
    wakeupChannel_->setReadCallback([this] { handleWakeup(); });    // 设置回调
    wakeupChannel_->enableReading();    // 注册监听
}

EventLoop::~EventLoop() {
    assert(isInLoopThread() && !runningOnce_ && !looping_);
    // Close epoll first, removing its registrations before destroying the
    // internal Channel and descriptor. No throwing syscalls in destruction.
    poller_.reset();
}

void EventLoop::wakeup() {
    const std::uint64_t value = 1;
    for (;;) {
        if (::write(wakeupFd_->fd, &value, sizeof(value)) == sizeof(value)) { return; }
        const int error = errno;
        if (error == EINTR) { continue; }
        if (error == EAGAIN) { return; }
        throw std::system_error(error, std::generic_category(), "eventfd write");
    }
}

void EventLoop::handleWakeup() {
    std::uint64_t value;
    for (;;) {
        if (::read(wakeupFd_->fd, &value, sizeof(value)) == sizeof(value)) { return; }
        const int error = errno;
        if (error == EINTR) { continue; }
        if (error == EAGAIN) { return; }
        throw std::system_error(error, std::generic_category(), "eventfd read");
    }
}

void EventLoop::queueInLoop(Task task) {
    if (!task) { throw std::invalid_argument("Task must not be empty"); }
    {
        std::lock_guard lock{taskMutex_};
        pendingTasks_.push_back(std::move(task));
    }
    wakeup();
}

void EventLoop::runInLoop(Task task) {
    if (!task) { throw std::invalid_argument("Task must not be empty"); }
    if (isInLoopThread()) { task(); }
    else { queueInLoop(std::move(task)); }
}

void EventLoop::doPendingTasks() {
    std::vector<Task> tasks;
    {
        std::lock_guard lock{taskMutex_};
        tasks.swap(pendingTasks_);
    }
    std::exception_ptr firstError;
    for (auto& task : tasks) {
        try { task(); }
        catch (...) { if (!firstError) { firstError = std::current_exception(); } }
    }
    if (firstError) { std::rethrow_exception(firstError); }
}

void EventLoop::quit() {
    quitRequested_.store(true, std::memory_order_relaxed);
    wakeup();
}

void EventLoop::loop() {
    assertInLoopThread();
    if (looping_ || runningOnce_) { throw std::logic_error("EventLoop cannot be reentered"); }
    looping_ = true;
    try {
        while (!quitRequested_.load(std::memory_order_relaxed)) {
            iterate(std::chrono::seconds{10});
        }
    } catch (...) {
        looping_ = false;
        throw;
    }
    looping_ = false;
}

void EventLoop::runOnce(std::chrono::milliseconds timeout) {
    // 安全检查，防止跨线程调用
    assertInLoopThread();
    // 防止回调里再调用runOnce
    if (looping_ || runningOnce_) { throw std::logic_error("EventLoop cannot be reentered"); }
    iterate(timeout);
}

void EventLoop::iterate(std::chrono::milliseconds timeout) {
    runningOnce_ = true;
    dispatching_ = true;
    try {
        // A previous I/O callback may have thrown after consuming the wakeup.
        // Pending tasks must never wait for another notification in that case.
        if (timeout.count() < 0 || timeout.count() > std::numeric_limits<int>::max()) {
            throw std::invalid_argument("Invalid poll timeout");
        }
        {
            std::lock_guard lock{taskMutex_};
            if (!pendingTasks_.empty()) { timeout = std::chrono::milliseconds{0}; }
        }
        // 等待数据，事件搬运与解包
        poller_->poll(timeout, activeEvents_);
        // 遍历分发
        for (const auto& active : activeEvents_) {
            // Earlier callbacks may have disabled this channel's interests.
            if (active.channel->isReading() || active.channel->isWriting()) {
                active.channel->handleEvent(active.events);
            }
        }
        activeEvents_.clear();
        dispatching_ = false;
        doPendingTasks();
    } catch (...) {
        activeEvents_.clear();
        dispatching_ = false;
        runningOnce_ = false;
        throw;
    }
    activeEvents_.clear();
    dispatching_ = false;
    runningOnce_ = false;
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
