# EventLoop

EventLoop 是整个事件驱动框架的中枢：它持有一个 EPollPoller（I/O 多路复用后端）、一个 eventfd 唤醒机制、一个跨线程任务队列，并负责"等待事件 → 分发 → 执行排队任务"这一循环。它还定义了线程模型：所有 I/O 和 Channel 操作必须在 loop 线程执行，跨线程只能通过 queueInLoop 投递。

## 整体架构
```text
EventLoop
├── ownerThread_      // 构造时所在的线程 = loop 线程
├── poller_           // EPollPoller，负责 epoll_wait 与注册
├── wakeupFd_         // eventfd，跨线程唤醒
├── wakeupChannel_    // 监听 eventfd 可读的 Channel
├── activeEvents_     // poll 结果的复用缓冲
├── pendingTasks_     // 跨线程任务队列（需加锁）
├── taskMutex_        // 保护 pendingTasks_
├── quitRequested_    // 原子退出标志
├── looping_          // 是否在 loop() 中
├── runningOnce_      // 是否在 iterate() 中
└── dispatching_      // 是否正在分发事件回调
```
职责分四块：

- 事件循环：loop() / runOnce() → iterate()，等待并分发；

- 跨线程唤醒：eventfd + wakeupChannel_，让其他线程能打断 epoll_wait；

- 任务队列：queueInLoop / runInLoop / doPendingTasks，把"其他线程想在本线程做的事"安全地串行化；

- 线程约束与注册入口：assertInLoopThread + updateChannel/removeChannel 委托给 poller。

## 二、唤醒机制：eventfd 为什么必要
epoll_wait 默认会阻塞到有 I/O 事件或超时。但跨线程场景下，别的线程可能：

投递了一个任务（queueInLoop），希望 loop 线程立刻执行，而不是等下一个 I/O 事件或 10 秒超时；

请求退出（quit()），希望 loop 线程尽快跳出。

如果没法打断 epoll_wait，这些请求就要等到下一次 I/O 或超时才被处理，延迟不可控。

eventfd 就是为此设计的：它是一个内核计数器 fd，write 一个 8 字节整数会让它变为可读，从而让 epoll_wait 返回。wakeupChannel_ 把它注册进 epoll，可读回调是 handleWakeup（把计数读走，清空可读状态）。

### WakeupFd 内部类
```c++
class EventLoop::WakeupFd : noncopyable {
    WakeupFd() : fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) { ... }
    ~WakeupFd() { const int saved = errno; (void)::close(fd); errno = saved; }
};
```

- EFD_NONBLOCK：读写不阻塞（wakeup 写满时返回 EAGAIN，代码里直接当成功处理）；

- EFD_CLOEXEC：exec 时自动关闭，防止泄漏到子进程；

- 析构保存/恢复 errno，与 EPollPoller 析构同一风格。

### wakeup()
```c++
void EventLoop::wakeup() {
    const std::uint64_t value = 1;
    for (;;) {
        if (::write(wakeupFd_->fd, &value, sizeof(value)) == sizeof(value)) return;
        const int error = errno;
        if (error == EINTR) continue;
        if (error == EAGAIN) return;     // 计数器已满/非阻塞，视为已唤醒
        throw std::system_error(...);
    }
}
```
- 循环处理 EINTR（被信号打断就重试）；

- EAGAIN 直接返回：说明 eventfd 计数已经非零（已处于"可读"状态），无需再写，唤醒目的已达成；

- 其他错误抛 system_error。

- 注意 wakeup() 是跨线程可调用的（queueInLoop/quit 都会调），它只写 fd，不碰 pendingTasks_ 之外的状态，是线程安全的。

### handleWakeup()
```c++
std::uint64_t value;
for (;;) {
    if (::read(wakeupFd_->fd, &value, sizeof(value)) == sizeof(value)) return;
    if (error == EINTR) continue;
    if (error == EAGAIN) return;
    throw ...;
}
```

把计数器读走，清空可读状态——否则 epoll_wait 会因为 eventfd 一直可读而忙循环（水平触发下反复报告 EPOLLIN）。这是 eventfd 唤醒机制必须配对"读空"的原因。

一个细节：这里读一次就返回，而不是循环读到 EAGAIN。因为 eventfd 的 read 是一次性取走整个计数值并清零（不是逐字节消费），读一次成功即已清空，无需循环。循环只是为了处理 EINTR。

## 构造
```c++
EventLoop::EventLoop()
    : ownerThread_(std::this_thread::get_id()),
      poller_(std::make_unique<EPollPoller>()),
      wakeupFd_(std::make_unique<WakeupFd>()),
      wakeupChannel_(std::make_unique<Channel>(*this, wakeupFd_->fd)) {
    wakeupChannel_->setReadCallback([this] { handleWakeup(); });
    wakeupChannel_->enableReading();
}
```
顺序很重要：先记 ownerThread_，再建 poller、eventfd、channel。enableReading() 会走 updateChannel → poller_->updateChannel，此时 poller 已就绪，且构造发生在 owner 线程，满足 assertInLoopThread。

wakeupChannel_ 的回调捕获 this，因此 EventLoop 必须比 wakeupChannel_ 活得久——由成员声明顺序保证（wakeupChannel_ 是后声明的成员，析构时先于前面的成员销毁）。不过这里析构逻辑有更明确的处理，见下。

## 运行逻辑
构造：建 poller + eventfd + wakeupChannel 并 enableReading（注册到 poller）；

loop() → iterate() → poller_->poll() 阻塞；

其他线程 queueInLoop(task) → 入队 + wakeup() → eventfd 可读 → poll 返回 → wakeupChannel_->handleEvent → handleWakeup 读空 → 本轮结束前 doPendingTasks() 执行任务；

其他线程 quit() → 置原子标志 + wakeup() → poll 返回 → 循环条件检查 → 退出；

业务 Channel::enableReading() → EventLoop::updateChannel → poller_->updateChannel → epoll 注册；就绪后 iterate 分发 → channel->handleEvent → 业务回调。

EventLoop 把"epoll 等待""跨线程唤醒""任务串行化""线程与状态约束"四件事缝合在一起，是这套库的调度核心。Channel 管意愿与分发，EPollPoller 管注册与等待，EventLoop 管循环、唤醒与线程边界——三者职责清晰，配合 eventfd 和任务队列，构成了一个可跨线程安全使用的单线程事件循环。