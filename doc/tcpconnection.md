# TcpConnection

TcpConnection 组合 Socket、Channel、输入 Buffer 和输出 Buffer，管理已建立 TCP 连接的非阻塞收发与关闭。Acceptor 只负责接受连接，再将 Socket 移动给它；主动 connect 不在本类范围内。

## 创建与 Echo 示例

```cpp
EventLoop loop;
std::vector<TcpConnection::Ptr> connections;
Acceptor acceptor{loop, InetAddress::loopbackIPv4(8080)};
acceptor.setNewConnectionCallback([&](Socket socket, const InetAddress&) {
    auto connection = TcpConnection::create(loop, std::move(socket),
        [](const TcpConnection::Ptr& self, Buffer& input) {
            self->send(input.readableView());
            input.retrieveAll();
        },
        [&](const TcpConnection::Ptr& self, std::error_code error) {
            // Record error if nonzero, then remove this connection from the owner.
            std::erase(connections, self);
        });
    connections.push_back(std::move(connection));
});
acceptor.start();
loop.loop();
// After quit: stop accepts and finish all queued connection cleanup before
// destroying the owning loop. A later TcpServer will coordinate this sequence.
acceptor.stop();
for (const auto& connection : connections) { connection->forceClose(); }
loop.runOnce(std::chrono::milliseconds{0});
```

create 必须在所属线程调用，要求已连接 Socket 和非空消息回调，成功后立即开启读取兴趣。第四个参数是可选关闭回调，第五个参数设置每个 Buffer 的上限（默认各 64 MiB）；初始存储各为 min(1 KiB, 上限)。地址通过 localAddress/peerAddress 查询并缓存。

## 所有权和线程

对象只能通过 create 获取 shared_ptr，构造和析构不公开。调用者（以后通常是 TcpServer）必须保存连接的强引用。Channel 回调捕获 weak_ptr，每次处理时 lock，避免回调与连接形成引用环。回调收到的 Ptr 引用只在调用期间有效，若要延后使用必须复制 Ptr。

自定义删除器确保实际析构在所属线程且不处于 I/O 分发阶段：如果最终引用在另一个线程或 I/O 回调中释放，就向 EventLoop 排队删除。这样既保护当前 Channel::handleEvent 的后续访问，也避免本批其他事件持有悬空指针。所有连接及排队的删除任务都必须在 EventLoop 销毁前清理。排队分配或注销失败导致无法安全析构时 terminate。

send、shutdown、forceClose、state、pendingOutputBytes 都要求所属线程。其他线程应捕获 Ptr 和拥有数据的 string，使用 loop.queueInLoop 调用；不要捕获短生命周期 string_view。当前没有直接跨线程 send，也没有给连接状态加锁。外部最后一个引用被释放会清理资源，但不会自动调用关闭回调；正常关闭通知应使用 forceClose/shutdown 或由对端关闭触发。

## 接收

每次读事件最多进行一次成功 recv，最多读取 64 KiB，直接写入 Buffer 的 span，然后 commitWritten 并调用消息回调。空间按需增长，不在首次读取时就预分配 64 KiB。LT 模式让剩余数据在后续轮次继续通知。

消息回调负责解析协议和 retrieve 消费数据；一次回调不代表一条完整消息。EAGAIN 返回等待下一次事件，EINTR 重试。若下一次读事件到来时输入缓冲已满且业务仍未消费，连接以 no_buffer_space 错误关闭。其他 recv 错误通过关闭回调报告。

读到 EOF 后停读，拒绝后续新发送，但会发送完已经缓存的输出后关闭。同步 Echo 响应因此不会仅因对端 shutdown(SHUT_WR) 就被丢弃；在 EOF 之后才完成的异步业务响应不受此策略支持。

## 发送

send 先复制完整数据到输出 Buffer，再尝试 send(MSG_NOSIGNAL)，避免 SIGPIPE 终止进程。超过输出上限时在发送任何本次新增字节前抛 length_error，旧数据保持不变。此版本总是先缓存，不提供绕过 Buffer 的直接发送优化。

每次处理最多发送 256 KiB。部分发送后消费已发送字节；遇到 EAGAIN 或达到预算仍有剩余，则开启写事件。写完清除写兴趣，避免持续可写造成忙循环。send 返回不代表对端已收到数据，甚至不代表本地输出 Buffer 已清空。pendingOutputBytes 只统计本地输出 Buffer，不包含内核发送队列。

## 状态与关闭

状态为 connected → disconnecting/closing → closed。

- shutdown：停止接受新 send，等待输出排空后 SHUT_WR，继续接收直到对端 EOF。重复调用无操作，没有关闭超时；对端不关闭时可用 forceClose。
- forceClose：进入 closing、禁用事件，排队最终注销和关闭，允许从消息回调调用。放弃未发完数据，默认关闭原因 error_code 为空。
- finishClose：在任务阶段注销 Channel、关闭 Socket、清空缓冲，再调用关闭回调一次。即使外部仍持有 Ptr，fd 也已经关闭。
- 消息回调抛异常：安排 operation_canceled 关闭，再把原异常传播给 runOnce/loop。必须继续处理任务队列才能完成延迟清理。关闭回调抛异常时资源已经清理，异常由 EventLoop 的任务异常策略传播。
- 网络错误：通过非零 error_code 的关闭回调报告；没有设置关闭回调则只关闭。内存分配和 epoll 更新失败仍可能直接抛异常，由上层处理。

回调在创建时固定，避免执行中替换函数造成生命周期问题。回调不要长期阻塞 EventLoop，不要捕获连接自身的强引用形成循环。

## 验证与边界

TcpConnection_test 验证 Acceptor → TcpConnection 二进制 Echo、分段接收、EOF 排空、2 MiB 输出与小发送缓冲导致的部分写、发送顺序与半关闭、容量限制、回调异常清理、I/O 回调和跨线程释放最后一个引用，以及对端 RST。

尚无 TcpServer 连接集合、连接超时、输出高水位通知、读暂停接口、写完成回调或自动线程分配。缓冲区有上限，但 EventLoop 的跨线程任务队列仍无容量上限；将大量 send 请求排队不是完整的背压方案。
