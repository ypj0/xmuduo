# Reactor：注册阶段

当前完成事件注册及 `epoll_wait → EventLoop::runOnce → Channel::handleEvent` 分发链路。尚未实现持续循环、跨线程唤醒或回调生命周期保护。

## 职责和所有权

Channel 继承 noncopyable，借用一个固定 fd 和所属 EventLoop 引用，保存读写兴趣和 std::function 回调，不关闭 fd、不执行网络 I/O。对象不允许移动，以保证以后 epoll_event.data.ptr 中的指针稳定。EventLoop 必须比 Channel 活得更久，Socket 的 fd 必须在 Channel 注销后才关闭。

EventLoop 记录创建线程，使用 unique_ptr 独占 EPollPoller。注册修改和注销必须从创建线程调用，跨线程或使用错误 EventLoop 抛 logic_error。Channel 的查询和回调配置也应只在所属线程使用；这些对象不提供并发同步。当前不限制一个线程创建多个 EventLoop。

EPollPoller 是内部实现层，只通过 EventLoop 使用。它拥有 epoll fd，使用 epoll_create1(EPOLL_CLOEXEC) 创建，析构关闭一次。channels_ 是 fd 到非拥有 Channel 指针的映射，仅记录成功注册的对象。

## 注册规则

| 已在 map 中 | 有读写兴趣 | 系统调用 |
|---|---|---|
| 否 | 否 | 无操作 |
| 否 | 是 | ADD |
| 是 | 是 | MOD |
| 是 | 否 | DEL |

读取兴趣映射到 EPOLLIN 和 EPOLLRDHUP，写入兴趣映射到 EPOLLOUT。默认水平触发，不启用 EPOLLET。修改兴趣不会执行回调。重复开启/关闭相同兴趣不执行系统调用。

ADD 前先插入 map，避免内核注册成功后内存分配失败；ADD 失败则撤销插入。DEL 成功后才删除 map 项，MOD 不改变 map。同一 fd 不能由同一 Poller 中的两个不同 Channel 注册。系统调用失败抛 system_error，Channel 恢复原来的兴趣；其他所有权/状态错误抛 logic_error。外部关闭或替换已注册 fd 属于违反生命周期约定，错误不会被静默忽略。

## 使用顺序

```cpp
EventLoop loop;
Socket socket;
Channel channel{loop, socket.fd()};
channel.setReadCallback([] { /* future read handler */ });
channel.enableReading();
channel.enableWriting();
channel.disableWriting();
channel.disableAll();
channel.remove();
```

disableAll 成功时已执行 DEL 并移除 map 项。remove 当前检查兴趣已经关闭、map 中没有同 fd 的注册，不重复 DEL；可重复调用，之后仍可重新开启兴趣。析构不会隐式注销，因此必须先 disableAll/remove 再销毁 Channel、关闭 Socket；后续事件循环还需解决一批就绪事件中的悬空指针和回调期间销毁问题。

## 验证

Channel_test 使用真实 epoll 注册，覆盖 ADD/MOD/DEL、重复开关、重新注册、同 fd 冲突、错误归属和跨线程调用、remove 前置条件、注册不执行回调，以及普通文件 ADD 失败后的兴趣与 map 回滚。

## 等待与分发

现在已实现 `runOnce(milliseconds)`：检查所属线程，调用 epoll_wait，遍历本轮 ActiveEvent 并调用 Channel::handleEvent。尚无持续 loop、quit、跨线程唤醒或任务队列。超时允许 0 到 INT_MAX 毫秒，0 表示立即检查；负数或超范围抛 invalid_argument。EINTR 作为本轮无事件返回，其他等待错误抛 system_error。

Poller 的事件 vector 初始有 16 个元素，按注册数量在等待前扩容并复用；输出 vector 也在等待前预留容量，避免已经取到事件后为存储结果分配失败。ReadyEvents 作为参数传入 Channel，不与读写兴趣混用；使用 readable/writable/peerClosed/hangup/error 表达就绪结果。

分发顺序：单独挂断且无可读或半关闭提示时调用 close；然后 error、read、write。peerClosed 进入 read 回调，由实际读取判断 EOF。读写回调还检查当前兴趣；较早回调 disableAll 后，本轮尚未处理的该 Channel 会被跳过。错误与挂断仅为通知，Channel 不执行 close 或网络读写。

runOnce 禁止重入；回调抛出异常时停止本轮分发、清空事件列表、恢复运行状态，再向调用者抛出。不保证本轮剩余回调被执行。默认 LT 下仍然就绪的 fd 可在下一轮再次通知。

生命周期约束：分发期间 removeChannel 会抛 logic_error，即使已 disableAll 也不能注销。回调可以修改兴趣，但不能销毁任何 Channel、关闭相关 fd、销毁 EventLoop，或替换正在执行的回调函数；应等 runOnce 返回后再注销和销毁。直接调用 handleEvent 也须遵守这些约束。当前没有自动延迟销毁和 weak_ptr tie 保护，禁止注销并不能阻止调用者直接 delete，不能将其当成完整的生命周期保障。

EventLoop_test 使用真实 eventfd 验证读写分发、读取后不重复通知、disableAll、重新注册、异常恢复、错误线程、重入与注销限制、超时参数及 40 个就绪 fd；另外用合成事件验证错误/挂断/半关闭的分发顺序。
