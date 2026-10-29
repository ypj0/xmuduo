# Socket

`Socket` 是 Linux TCP 文件描述符的独占所有者，继承项目的 `noncopyable`。地址由 `InetAddress` 表示，socket 的生命周期由此类管理。

## 使用

```cpp
Socket listener; // IPv4; Socket::Family::ipv6 creates IPv6
listener.setReuseAddress(true);
listener.bind(InetAddress::loopbackIPv4(8080));
listener.listen();
if (auto connection = listener.accept()) {
    const auto peer = connection->peerAddress();
    // Transfer ownership to TcpConnection later.
}
```

## 实现步骤

1. 构造调用 `socket`，原子设置 `SOCK_NONBLOCK | SOCK_CLOEXEC`。非阻塞用于事件循环，CLOEXEC 防止 fd 泄漏到 exec 后的新程序。
2. 析构调用一次 `close`，不抛异常、不重试 EINTR，并保留 errno。fd 为 -1 时无操作。
3. 移动构造/赋值转移 fd，将源对象置为 -1。移动赋值先关闭目标原 fd；复制操作由 noncopyable 禁用。移动后只能析构、重新移动赋值或查询 fd；其他系统调用通常报 EBADF。对象本身不提供并发同步，生命周期应由所属 EventLoop 管理。
4. `bind` 使用 InetAddress 的地址与长度；`listen` 默认 backlog 为 SOMAXCONN，要求正数，实际队列上限还受内核设置限制。
5. `accept` 使用 accept4，为新 fd 同时设置非阻塞与 CLOEXEC；EINTR 重试，EAGAIN/EWOULDBLOCK 返回空 optional，成功返回拥有连接 fd 的 Socket，不做额外堆分配。其他错误（包括 EMFILE、ECONNABORTED）抛 system_error，由后续 Acceptor 决定重试或退避策略。
6. `localAddress`/`peerAddress` 使用 getsockname/getpeername。绑定端口 0 后可查询系统分配的端口；`shutdownWrite` 只关闭发送方向，接收方向仍保留。

## socket 选项

提供 `setReuseAddress`、`setReusePort`、`setKeepAlive`、`setTcpNoDelay` 和 `setIPv6Only`，均可开启或关闭，失败抛出 system_error。复用选项和 IPv6Only 应在 bind 前配置；未显式设置的选项遵从系统默认值。KeepAlive 仅启用系统保活，未配置探测周期；TCP_NODELAY 控制 Nagle 算法。IPv6 是否接收 IPv4 映射连接需显式选择策略。

`fd()` 是借用句柄，不可外部 close，也不可用另一个所有者重复管理。当前不封装主动连接与收发；后续 Connector/TcpConnection 将负责非阻塞 connect 的完成判定、部分读写、EAGAIN 和缓冲管理。直接 send 时应使用 MSG_NOSIGNAL 或制定 SIGPIPE 策略。

构造、系统调用失败抛 `std::system_error`，非法 Family 或 backlog 抛 `std::invalid_argument`。这里必须手写析构和移动操作，因为管理的是裸整数 fd，而不是自动释放的智能指针。

## 测试

Socket_test 使用回环地址和端口 0，验证 fd 标志、所有权移动与关闭、socket 选项、空 accept、地址占用、IPv4/IPv6 真实连接与地址查询、半关闭后仍可接收。使用 poll 限时等待，不连接外部网络；环境未提供 IPv6 协议时会明确报告跳过该分支。
