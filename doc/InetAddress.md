# InetAddress 类设计文档

## 1. 设计目的
`xmuduo::InetAddress` 用于封装底层的 IPv4 套接字地址结构体（`sockaddr_in`）。它是轻量值类型，内部仅保存一个 `sockaddr_in`，继承自 `copyable`，适合按值传递、复制和存储。

当前接口提供两种构造方式：从 IP 字符串与端口号构造，或直接包装已有的 `sockaddr_in`；同时提供将内部网络字节序地址转换回可读字符串（IP、IP:Port）及端口号的接口。该类用于在应用层与底层系统调用（如 `bind`、`accept`、`connect`）之间充当翻译官，隐藏底层字节序转换和地址格式转换的细节。

## 2. 数据表示
```cpp
class InetAddress : public copyable {
public:
    explicit InetAddress(uint16_t port, std::string ip = "127.0.0.1");
    explicit InetAddress(const sockaddr_in &addr)
        : addr_(addr)
    {}

    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t toPort() const;

    const sockaddr_in* getSockAddr() const { return &addr_; }

private:
    sockaddr_in addr_;
};
```
addr_ 是系统原生的 IPv4 套接字地址结构体，包含协议族（sin_family）、网络字节序端口（sin_port）和网络字节序 IP 地址（sin_addr）。默认构造带参版本允许指定端口与 IP（默认 127.0.0.1）；另一个构造函数允许从系统调用返回的 sockaddr_in 直接初始化。所有构造函数均使用 explicit，防止隐式类型转换带来的意外行为。
## 3. 构造与地址初始化
构造函数负责将人类可读的 IP 字符串和主机字节序端口转换为网络传输所需的二进制格式：

```cpp
std::memset(&addr_, 0, sizeof(addr_));
addr_.sin_family = AF_INET;
addr_.sin_port = htons(port);

if (::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr) <= 0) {
    addr_.sin_addr.s_addr = INADDR_NONE;
}
```
- std::memset 用于将结构体清零，防止残留垃圾数据。

- AF_INET 明确指定使用 IPv4 协议族。

- htons（Host to Network Short）将主机字节序端口转换为网络字节序（大端）。

- inet_pton 将点分十进制 IP 字符串转换为 32 位网络字节序整数。相比已废弃的 inet_addr，inet_pton 能更安全地处理非法格式和广播地址，失败时回退为 INADDR_NONE。

## 4. 转换与格式化：toIp / toIpPort / toPort
- toIp()：使用 inet_ntop 将二进制 IP 地址转换为点分十进制字符串。内部使用 std::array<char, INET_ADDRSTRLEN> 作为局部缓冲区，避免裸数组和魔数。若转换失败（返回 nullptr），回退为 "0.0.0.0"。

- toIpPort()：复用 toIp() 获取 IP 字符串，通过 ntohs 将网络字节序端口还原为主机字节序，使用 std::string 拼接和 std::to_string 生成 "IP:Port" 格式。此实现完全摆脱了 sprintf 和 strlen，消除了缓冲区溢出风险。

- toPort()：直接返回 ntohs(addr_.sin_port)，即人类可读的端口号。

## 5. 与底层网络调用的关系
getSockAddr() 返回内部 sockaddr_in 的 const 指针。当需要调用系统 API（如 bind(fd, addr.getSockAddr(), sizeof(sockaddr_in)) 或 connect）时，通过该接口将底层结构体暴露给内核，同时 const 修饰防止外部意外修改类内部状态。

## 6. 当前边界
- 仅支持 IPv4（AF_INET），不支持 IPv6（AF_INET6）。

- IP 字符串解析失败时静默回退为 INADDR_NONE，未抛出异常；调用方需自行确保输入合法性。

- 默认 IP 地址为 127.0.0.1（本机回环），适用于本地测试，生产环境需显式指定。

- getSockAddr() 返回的指针生命周期与 InetAddress 对象绑定，调用方不应在对象销毁后继续使用该指针。

声明位于 include/InetAddress.h，实现位于 src/InetAddress.cc。