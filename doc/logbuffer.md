# LogBuffer

`include/LogBuffer.h` 定义固定容量的单条日志组装缓冲区 `LogBuffer<Capacity>`，默认容量 4096 字节。它不负责日志输出。

```cpp
LogBuffer<> buffer;
if (buffer.appendFormat("fd={}, elapsed={:.2f}ms", 42, 1.25)) {
    auto text = buffer.view();
    // fd=42, elapsed=1.25ms
}
```

`append(text)` 追加原始字节；`appendFormat(format, args...)` 使用 C++20 `std::format_string` 检查格式与类型，通过 `std::format_to_n` 直接写入剩余空间。二者成功返回 true，容量不足返回 false，允许恰好填满。调用者必须处理失败；当前不自动截断或扩容。

格式化可能先写入部分剩余空间，但只有完整结果放得下才更新有效长度。因此失败时原来的有效内容保持不变，后续追加会覆盖这部分未提交数据。格式化异常向上传播，同样不提交长度，故 `appendFormat` 不标记 noexcept。

`size()` 返回有效字节数，`available()` 返回剩余容量，`capacity()` 返回总容量，`empty()` 判断是否为空。`reset()` 仅清空长度以便复用；`data()` 不保证零字符结尾，输出时应使用 `view()` 或指针加长度。视图不拥有数据，缓冲区修改或销毁会影响其有效性。

格式化参数不得引用当前缓冲区存储，以免读写干扰。缓冲区本身不动态分配；标准库或自定义 formatter 仍可能分配内存。`format_to_n` 限制写入量，但仍计算完整结果长度，因此不保证巨大格式化输入的处理耗时有固定上界。每次调用应独占一个缓冲区，不提供内部同步。

`tests/LogBuffer_test.cc` 覆盖原始追加、空输入、自身视图追加、嵌入零字节、格式说明符、数值极值、精确填满、容量不足和异常后的复用。
