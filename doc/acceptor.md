# Acceptor

Acceptor 把监听 Socket 与 Channel 组合起来：EventLoop 等待可读事件，Channel 调用 handleRead，后者接受连接并调用用户提供的新连接回调。它不负责连接建立后的读写、协议解析或连接容器管理。

## 使用

```cpp
EventLoop loop;
Acceptor acceptor{loop, InetAddress::loopbackIPv4(8080)};
std::vector<Socket> connections;
acceptor.setNewConnectionCallback(
    [&](Socket socket, const InetAddress& peer) {
        // Copy peer if it must outlive this callback.
        connections.push_back(std::move(socket));
    });
acceptor.start();
loop.loop();
acceptor.stop();
```

示例只是接受并保留连接；后续 TcpConnection 会接管 Socket，并为它建立独立 Channel。调用者需要通过 quit 等机制结束 loop。

## 分步实现

1. 构造时确认所属线程，按 InetAddress 地址族创建非阻塞、CLOEXEC Socket，设置 SO_REUSEADDR，绑定地址；此时尚未 listen 或注册事件。端口 0 可用 localAddress 查询实际端口。第三个构造参数控制 SO_REUSEPORT，默认关闭。IPv6 明确设置 IPV6_V6ONLY=true，IPv4 与 IPv6 使用独立监听器。
2. 设置非空 std::function<void(Socket, const InetAddress&)> 回调。Socket 按值移动交付，不暴露需要手工 close 的裸所有权。回调不保留 Socket 时，返回后自动关闭；peer 引用只在本次调用期间有效。
3. start(backlog) 调用 listen，再开启 Channel 读兴趣。必须先设置回调；重复 start 不更新已启用的监听。注册失败会抛异常，兴趣保持关闭，但内核可能已经进入监听状态，调用者可重试或销毁。
4. handleRead 最多进行 64 次 accept 尝试，EAGAIN 时结束；LT 模式确保尚有连接时下一轮再次通知。每轮预算防止连接大量涌入时持续占用线程，不代表内核调用或用户回调有严格时间上限。
5. Socket::accept 已重试 EINTR。Linux 文档列出的暂时网络错误和 ECONNABORTED 在本轮预算内继续尝试。其他接受/地址查询错误（包括 EMFILE/ENFILE），以及用户回调异常，会暂停接受并向 runOnce/loop 调用者传播，避免反复触发错误造成忙循环。若暂停本身失败，则传播暂停错误。

## 暂停、销毁和线程约束

所有接口均在所属 EventLoop 线程使用。stop 幂等，可在新连接回调中调用，禁用读兴趣并停止当前接受批次。它只是暂停：监听 fd 仍然打开且占用端口，客户端可能继续完成握手并进入内核队列；再次 start 后继续接受。已转交的新连接不会受影响。当前没有永久关闭接口，释放监听端口需销毁对象。

不允许在新连接回调执行期间替换该回调。回调抛异常时，未转移出去的 Socket 自动关闭；已经交给外部容器的 Socket 不会被回滚。

析构先暂停、注销 Channel，再按成员逆序销毁 Channel 和 Socket。必须在所属线程、整批 I/O 分发结束后析构，且 EventLoop 必须仍然存在。可以在回调中使用 queueInLoop 安排销毁；不能使用同线程立即执行的 runInLoop。延迟任务捕获必须保证对象和引用有效。违反析构线程/分发约束或注销失败时 terminate，不能静默留下悬空注册。

## 当前边界

当前未实现 muduo 的预留 fd 应急策略、自动退避重试、连接数量限制或线程池分配。资源耗尽后需上层处理异常，释放资源后显式 start 恢复；重新调用 loop 不会自动恢复监听。新连接回调仍应短小，避免阻塞其他连接。

## 测试

Acceptor_test 使用 IPv4/IPv6 回环与临时端口，验证 70 个连接的批次预算、Socket 所有权与标志、对端地址、暂停恢复、线程约束、回调异常关闭与恢复、回调内暂停和延迟析构。当前该集成测试需要环境启用 IPv6 回环，不访问外部服务。
