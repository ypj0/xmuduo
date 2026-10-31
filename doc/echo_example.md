# Echo 示例

源码位于 `examples/echo_server.cc`，使用 TcpServer 接受连接，把收到的二进制数据原样发回。没有协议分帧，不能将一次 recv 当成一条完整消息。

## 构建和运行

```sh
cmake -S . -B build/echo -DCMAKE_BUILD_TYPE=Debug -DXMUDUO_BUILD_EXAMPLES=ON
cmake --build build/echo --target echo_server -j 4
./build/echo/examples/echo_server
```

默认监听 127.0.0.1:8080。可指定数值 IP 和端口，端口 0 自动分配并在启动时打印实际地址：

```sh
./build/echo/examples/echo_server 127.0.0.1 9000
./build/echo/examples/echo_server ::1 9000
./build/echo/examples/echo_server --help
```

客户端可以用 `nc 127.0.0.1 8080` 输入文本，或使用 Python 验证二进制与半关闭：

```python
import socket

payload = b"hello\x00echo\n" * 10000
with socket.create_connection(("127.0.0.1", 8080), timeout=5) as client:
    client.sendall(payload)
    client.shutdown(socket.SHUT_WR)
    response = bytearray()
    while chunk := client.recv(65536):
        response.extend(chunk)
    assert response == payload
print("echo verified")
```

## 退出与边界

Ctrl+C（SIGINT）或 SIGTERM 仅在信号处理函数中设置 sig_atomic_t 标志，不调用日志、锁、queueInLoop 或服务器接口。主线程使用 100ms 的 runOnce 检查标志，再调用 stop 并继续处理任务直到 stopped；实际响应时间还受回调耗时影响。此示例是单线程程序，用于展示完整使用流程；更复杂的 Linux 服务可用 signalfd 将信号接入 Channel，避免周期检查。

stop 是强制停止，会关闭活动连接，可能放弃未发完的数据。客户端正常 shutdown 写端时，服务器则先发完已缓存的 Echo 数据再关闭。配置和运行异常会尝试停止、清理并以非零退出码报告；不提供自动恢复资源耗尽等故障的能力。

示例默认只监听回环地址，不设置额外线程池或更改系统全局 SIGPIPE 策略；连接发送沿用 MSG_NOSIGNAL。示例目标由 XMUDUO_BUILD_EXAMPLES 控制，默认关闭，不影响库的默认构建。
