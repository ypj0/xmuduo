# AsyncLogSink

`AsyncLogSink` 是接在 `Logger` 输出回调后的有界异步后端，定义于 `include/AsyncLogSink.h`。业务线程只复制完整日志记录并放入队列；后台线程批量写入 `RollingFileSink`，后者负责文件大小滚动。

## 构造和使用

```cpp
#include "AsyncLogSink.h"
#include "Logger.h"
#include <memory>

auto sink = std::make_shared<AsyncLogSink>("logs", "server", 64 * 1024 * 1024);
Logger::setOutput(
    [sink](std::string_view message) { sink->append(message); },
    [sink] { sink->flush(); });
LOG_INFO("request completed");
Logger::flush();

// Shutdown order: stop new logs, detach callbacks, drain and join explicitly.
Logger::setOutput([](std::string_view) {}, [] {});
sink->stop();
```

构造参数依次是日志目录、basename、滚动阈值、队列容量（默认 1 MiB）和后台刷新周期（默认 3 秒）。容量至少要容纳一条记录的队列记账开销；记录太大时 `append()` 立即抛出 `length_error`。容量耗尽时生产者等待，提供背压，不丢弃日志。容量统计已入队和后台正在写入的记录，按记录字节和固定每条记账开销估算，是待处理载荷上限，不是精确的进程 RSS 上限。传入 `0ms` 禁用周期刷新，但 `flush()` 和停止排空仍会显式刷新；负刷新周期非法。

`append()` 在返回前复制 `string_view`，调用者随后可修改或释放原数据。它只确认记录被队列接受，不代表已经写入文件。`flush()` 记录当前已接受序号的屏障，等待截至该序号的记录写完并完成文件刷新；之后并发提交的记录不会推进屏障目标，后台已取走的批次可能一并完成。`stop()` 拒绝新记录，排空已接受队列、刷新文件并等待工作线程退出，可重复调用。停止后 `append()` 和 `flush()` 抛出异常。写入错误会保存在后台并由后续 `append()`、`flush()` 或显式 `stop()` 抛出。析构也会停止并等待线程，但不能抛出错误；需要观察退出错误时应先显式调用 `stop()`。

## 并发与错误语义

多个生产者可以并发调用 `append()` 和 `flush()`。内部互斥锁只保护队列与屏障状态，磁盘操作由一个后台线程串行执行。Logger 的回调锁在调用 `append()` 时持有，因此复制入队或等待队列容量期间其他 Logger 输出会等待。后台线程不获取 Logger 的锁；但显式 `Logger::flush()` 会持有该锁等待刷新完成。

后台复用一个线程处理写入和周期刷新，内部 RollingFileSink 的定时线程被禁用。刷新间隔按 steady_clock 计算，持续有日志入队也会检查到期；调度、批量写入及 I/O 耗时可能延后刷新，不保证严格的时间上限。刷新仍是 `fflush`，不等于 `fsync`。

目前每条消息使用 `std::string` 保存，在入队时可能分配内存；批量出队减少队列锁竞争，但文件后端仍逐条追加以保持滚动边界。这是有背压的正确性基础版本，不承诺无锁、零分配或比同步模式在所有负载下都快。

后台发生写入或刷新错误后，sink 进入失败状态，唤醒被队列容量阻塞或等待 `flush()` 的线程，并丢弃尚未写出的队列项。`flush()` 对该屏障内全部记录成功后才返回；失败不回滚已经写入的记录。`append()` 可能因队列背压阻塞，后台异常或 `stop()` 会唤醒并使其失败。

析构等待正在执行的文件 I/O，不承诺硬实时退出。销毁对象前，调用者必须先停止其他访问线程，并从 Logger 输出回调中解绑或保证回调不会再调用 sink。通常应按示例顺序解绑回调再 `stop()`。

## 测试

`AsyncLogSink_test` 覆盖消息复制、空消息和二进制数据、容量错误、队列背压、并发生产者、flush 屏障、周期刷新、滚动顺序、停止排空与重复停止、写入错误传播和 Logger 回调接入。
