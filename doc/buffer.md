# Buffer

Buffer 是连接收发用的连续字节缓冲区，区别于每条日志使用的固定容量 LogBuffer。继承 noncopyable，当前不支持复制或移动，由连接对象直接拥有；不提供内部锁，所有访问应在所属 EventLoop 线程进行。

## 数据布局

```text
[ 已消费、可复用空间 | 可读数据 | 尾部可写空间 ]
0                  reader     writer        capacity
```

始终满足 `0 <= reader <= writer <= capacity <= maxCapacity`。readableBytes 为 writer-reader，writableBytes 为 capacity-writer。capacity 表示当前可用存储大小，不代表分配器精确占用的内存。

构造 `Buffer(initialCapacity = 1024, maxCapacity = 64 * 1024 * 1024)`，默认初始 1 KiB、最大 64 MiB，可使用初始容量 0；上限必须大于零且不小于初始容量，否则抛 invalid_argument。没有在构造时预分配全部上限容量。

## 接口

- readableView 返回 string_view，支持内嵌零字节，不复制数据。
- append(string_view) 复制追加数据。尾部足够时直接写入；不足但消费空间足够时 memmove 压紧；仍不足时按倍增或实际需求扩容，上限为 maxCapacity。超限抛 length_error，不丢弃旧数据。
- ensureWritable(n) 保证至少 n 个尾部可写字节；不增加可读字节数。
- writableSpan(minimum = 1) 先保证容量，再返回整个尾部可写 span。可用于后续 recv 直接写入，避免额外中间缓冲复制。
- commitWritten(n) 把实际写入的 n 字节转成可读数据。必须保证这些字节已初始化，且 n 不超过此前获取的可写区域；超过当前 writableBytes 抛 out_of_range。
- retrieve(n) 消费前 n 个可读字节，超出可读范围抛 out_of_range。普通消费只移动下标；全部消费后读写下标归零。
- retrieveAll 清空逻辑数据，不释放容量，也不擦除原字节。
- retrieveAsString(n) 复制前 n 字节到拥有内存的 string，然后消费这些字节；分配失败不会提前消费数据。

## 示例

```cpp
Buffer input;
input.append("hello\npartial");
auto data = input.readableView();
const auto end = data.find('\n');
if (end != std::string_view::npos) {
    auto line = input.retrieveAsString(end + 1);
    // input retains "partial" for the next read.
}
```

TCP 是字节流：一次接收可能不是完整消息，也可能包含多条消息。Buffer 只保存字节，协议解析负责决定消费多少。网络读取逻辑未来放入 TcpConnection：获取可写 span，recv 返回正数后 commitWritten；EOF、EAGAIN 和错误不提交字节。接近容量上限时应根据剩余预算选择读取长度，不能无条件要求一大块空间。

## 生命周期与性能

所有返回的 view/span 都借用内部内存；后续修改可能搬移、扩容或覆盖数据，应在修改后重新获取。获取 writableSpan 后，写入并 commitWritten 之前不要调用其他修改方法，也不要保存该 span 给异步任务使用。Buffer 无法证明外部写入确实完成，commitWritten 的初始化约定由调用者负责。

支持 append 自己的可读视图：这种特殊情况先复制快照，避免扩容或压紧使源视图失效。普通外部数据追加不创建该快照。不要传入已失效或未初始化内存的视图。

容量上限是单 Buffer 的存储策略，不是进程内存硬上限：扩容期间旧/新存储短暂共存，自身追加还有快照，分配器也有额外开销。上限触发后应由连接层决定暂停读、拒绝消息或关闭连接。当前不提供自动缩容、头部预留、协议编码、readv 或文件描述符 I/O。

## 验证

Buffer_test 覆盖二进制字节、部分消费、压紧、扩容、容量边界、越界异常后的数据保留、零初始容量、span 提交、自身追加，以及固定种子的 5000 次混合操作与 string 模型对照。
