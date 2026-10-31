# TcpServer

单 EventLoop TCP 服务器：Acceptor 接受连接，TcpServer 保存连接强引用，TcpConnection 负责收发与关闭。网络 I/O 和业务回调均在指定 EventLoop 线程执行；没有内置工作线程池。

## Echo 示例

```cpp
#include "TcpServer.h"
#include "EventLoop.h"

EventLoop loop;
TcpServer server{loop, InetAddress::loopbackIPv4(8080)};
server.setMessageCallback([](const TcpConnection::Ptr& connection, Buffer& input) {
    connection->send(input.readableView());
    input.retrieveAll();
});
server.setStoppedCallback([&] { loop.quit(); });
server.start();
loop.loop();
```

示例持续服务，直到应用在所属线程调用 server.stop()；其他线程可通过 loop.queueInLoop 提交停止请求，前提是引用的 server 和 loop 仍然存活。不要把 C++ 回调或 queueInLoop 直接用作 POSIX 信号处理函数。线程间共享消息时应捕获拥有内存的数据，而非临时 string_view。

## 配置

构造函数绑定地址，端口 0 可用 localAddress() 查询实际端口。Options 提供 maxConnections（默认 1024）、bufferLimit（每个输入/输出 Buffer 默认各 64 MiB）、backlog（SOMAXCONN）和 reusePort（默认 false）。数量、缓冲上限和 backlog 必须大于零。IPv6 监听沿用 Acceptor 的 IPv6Only=true。

start 前必须设置非空消息回调；可选的 setCloseCallback 接收已关闭连接及错误码，setStoppedCallback 在服务器停止完成时执行。配置在首次 start 或 stop 后冻结，避免执行中替换 std::function。start 在 running 状态重复调用是无操作或恢复先前因接受错误暂停的 Acceptor；stop 后不可重新 start。

达到 maxConnections 时，已 accept 的额外连接直接由 Socket RAII 关闭，不加入连接集合。客户端可能已经完成握手，因此这不是阻止握手的协议级拒绝机制。bufferLimit 是单缓冲限制，不是服务器总内存上限；应按应用负载设置这些参数。

## 连接集合和回调

内部 map 用 TcpConnection 对象地址作键，保存 Ptr，不用可能被内核复用的 fd 作身份。收到关闭通知时先移除集合，再调用用户关闭回调。connectionCount 包含尚未完成关闭的连接。

内部共享 Impl 保存状态，Acceptor 和 TcpConnection 的回调只保存 weak_ptr<Impl>，避免引用环。关闭通知的用户回调抛异常时，停止状态仍会完成检查，之后向 EventLoop 传播第一个异常。业务回调需短小；耗时或阻塞工作应由应用交给工作线程。

## 状态与停止

状态为 ready → running → stopping → stopped，也可在 ready 时直接 stop。

stop 幂等，可在消息回调中调用：

1. 向 EventLoop 提交清理任务，标记 stopping，暂停接受新连接。
2. 在任务阶段销毁 Acceptor，注销监听 Channel 并释放监听端口。
3. 对全部连接请求 forceClose，后续任务完成连接注销、关闭 fd 和关闭通知。
4. 监听器已释放且连接集合为空时，标记 stopped，只调用一次停止回调。

stop 是强制停止，可能丢弃尚未发完的数据，不等同于 TcpConnection::shutdown 的排空与半关闭。当前没有带超时的优雅停服。停止后已保存的 localAddress 仍可用于查询原监听地址，不表示端口仍被占用。

推荐先 server.stop，在停止回调中 loop.quit；直接先退出循环可能让清理任务无法执行。若使用 runOnce 驱动，应持续调用到 state()==stopped。停止过程中的分配、唤醒或内核错误仍可能抛异常，不承诺进程资源耗尽时正常完成。

## 析构约束

TcpServer 不可复制或移动。所有方法和析构都必须在所属线程，且不能在 I/O 分发中直接销毁服务器；可排队到任务阶段再销毁。

析构自动请求停止，并禁止后续用户回调，避免继续使用已经销毁的业务捕获对象。内部清理状态由排队任务维持；析构返回不等于资源清理完成。EventLoop 必须继续处理这些任务，并晚于服务器、所有连接引用及删除任务销毁。最清晰的做法是显式 stop 并等待 stopped 后再析构。违反析构线程/分发约束或无法提交清理任务时 terminate。

## 测试

TcpServer_test 验证多客户端二进制 Echo、连接集合移除、连接数上限、回调中停止、重复停止和停止回调仅一次、监听端口释放与重绑定、服务器析构后的延迟清理、错误线程、配置约束及关闭回调异常后的停止完成。

目前尚未提供新连接建立通知、连接空闲超时、连接池或多 I/O 线程分配。这些可在基础服务器稳定后逐步添加。
