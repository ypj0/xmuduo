# Channel
Channel 是这套网络库里的事件源抽象层——它把一个 fd + 它关心的读写状态 + 对应的回调打包成一个对象，向下给 EPollPoller 提供意愿，向上给业务代码提供事件分发入口。所有 epoll 操作都委托给 EventLoop（进而 EPollPoller）。

### 整体架构中的位置

```text
业务代码
   │  setReadCallback(...) / enableReading()
   ▼
Channel ──update()──► EventLoop ──► EPollPoller ──► epoll_ctl
   ▲                      │
   │                      └──poll()──► 得到 ActiveEvent{Channel*, ReadyEvents}
   │
   └──handleEvent(ReadyEvents)──► 调用对应回调
```
