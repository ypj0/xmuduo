# InetAddress

`InetAddress` 是 IPv4/IPv6 数值套接字地址的可复制值类型，声明在 `include/InetAddress.h`，实现位于 `src/InetAddress.cc`。它保存 `sockaddr_storage` 和实际地址长度，可直接传给 `bind`、`connect` 等系统调用。

## 构造

```cpp
InetAddress local{8080};                    // 默认 127.0.0.1
InetAddress remote{443, "2001:db8::1"};
auto any = InetAddress::anyIPv6(8080);       // [::]:8080
auto loopback = InetAddress::loopbackIPv4(); // 127.0.0.1:0
```

端口以主机字节序传入和返回，内部存为网络字节序。地址字符串必须是数字 IPv4 或 IPv6 字面量；不做 DNS 解析。非法地址、内嵌零字符以及不支持的地址族会抛出 `std::invalid_argument`。DNS 解析应放在独立的 resolver 中，避免地址值类型执行潜在阻塞操作。

可用 `InetAddress(const sockaddr*, socklen_t)` 包装 `accept`、`getsockname` 等系统调用产出的 IPv4/IPv6 地址；函数会验证指针、长度和地址族，并复制数据，不保留传入指针。

## 查询与系统调用

`family()` 返回 `AF_INET` 或 `AF_INET6`；`port()` 返回主机字节序端口；`ip()` 返回规范化的 IP 文本；`ipPort()` 返回 `IPv4:port` 或 `[IPv6]:port`，方括号用于避免 IPv6 冒号与端口分隔符混淆。

```cpp
InetAddress address{8080, "0.0.0.0"};
if (::bind(fd, address.sockaddrPtr(), address.sockaddrLength()) < 0) {
    // handle errno
}
```

`sockaddrPtr()` 返回指向对象内部地址的只读指针，生命周期受该 `InetAddress` 对象约束。若 API 需要写入地址，应使用本地 `sockaddr_storage` 缓冲和长度变量，系统调用完成后再用构造函数复制进对象。

## 当前边界

- 只支持 IPv4 和 IPv6，不包含 Unix domain socket。
- 不做主机名解析，也不隐藏 `bind`、`connect` 等系统调用错误。
- IPv6 scope id 当前通过 sockaddr 构造/查询保留在底层结构中，但 `ip()` / `ipPort()` 不输出 `%interface` 区域标识；链路本地地址若需要文本往返，应后续扩展该接口。
- 该类型只表示地址，不拥有 socket，也不负责连接生命周期。

`tests/InetAddress_test.cc` 覆盖双栈解析和格式化、端口字节序、通配/回环地址、复制、sockaddr 转换及非法输入。
