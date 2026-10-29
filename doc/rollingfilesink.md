# RollingFileSink

同步按大小滚动的文件后端，声明在 `include/RollingFileSink.h`。继承 `noncopyable`，使用 `unique_ptr<FileSink>` 管理当前文件，默认析构自动关闭文件。

## 接口

`RollingFileSink(directory, basename, rollSize, flushInterval = 3s)` 在已有目录中立即创建第一个文件。`rollSize` 单位为字节，必须大于零；`basename` 必须非空，不能是 `.`、`..`，不能含 `/` 或零字符。参数错误抛出 `invalid_argument`，文件操作错误抛出 `system_error`。不会创建目录。

`append(message)` 将一次调用视为一条完整记录，不补换行，支持零字节。空消息无操作。当前文件非空且新记录会超过阈值时，先滚动再写入；恰好达到阈值不立即创建下一文件。超过阈值的单条记录完整写入一个文件，下一条非空记录触发滚动。因此阈值不是文件大小的硬上限。

`flush()` 刷新当前文件的 stdio 缓冲，不提供 `fsync` 持久化保证。默认每 3 秒由后台线程自动刷新一次；传入零间隔可关闭定时刷新，负间隔非法。定时刷新使用同一互斥锁协调 `append`、滚动和显式刷新，因此可直接并发调用这些接口。

## 实现步骤

1. 构造时检查参数，通过 `roll()` 创建初始文件。
2. 文件名为 `basename.序号.log`，序号从零递增。使用 FileSink 的独占创建，遇到 `EEXIST` 跳过该名称，其余错误向上传播。重启或多个实例使用同一前缀都不会覆盖已有文件。序号可能有空洞，接近整数上限时抛出 `overflow_error` 防止回绕。
3. 写入前以减法比较剩余容量，避免字节数相加溢出；仅在 `append` 成功后增加计数。计数包括 stdio 尚未刷新的字节。
4. 滚动先显式刷新旧文件，再创建新文件，创建成功后替换指针并清零计数。刷新或创建失败时保留旧文件对象，该次记录尚未开始写入。FileSink 写入失败则可能已经部分写入，不能安全地重试整条日志，调用者应停止使用该 sink 并处理错误。

析构会停止并等待刷新线程，然后关闭当前文件；关闭错误无法报告。后台刷新若失败，会停止定时刷新，并由下次 `append()` 或 `flush()` 重新抛出该异常；析构本身不抛出。独占创建后 `fdopen` 失败可能留下空文件，详见 `filesink.md`。

后台线程使用 C++20 `std::jthread`，通过支持 `stop_token` 的条件变量等待。线程成员声明在最后，析构时先请求停止并等待线程退出，再销毁锁和文件。调用者仍须保证析构时没有其他线程访问对象。定时周期从上次刷新完成后计算，可能受线程调度、锁竞争和文件 I/O 延迟影响，不是严格的时间上限；析构也会等待正在进行的 I/O。

## 接入 Logger

```cpp
#include "Logger.h"
#include "RollingFileSink.h"
#include <memory>

auto sink = std::make_shared<RollingFileSink>("logs", "server", 64 * 1024 * 1024);
Logger::setOutput(
    [sink](std::string_view message) { sink->append(message); },
    [sink] { sink->flush(); });
LOG_INFO("server started");
Logger::flush();
```

`logs` 目录须预先存在。Logger 已串行化回调；sink 自身也会同步后台刷新与追加。普通追加不生成文件名，也不检查文件系统，只做阈值判断和缓冲写入。每个新实例从零开始寻找空闲序号，大量历史文件会增加启动成本。当前不实现按日期滚动、历史清理或后台日志写入；后台线程只负责定时刷新。

## 测试

`RollingFileSink_test` 覆盖阈值边界、写入前滚动、超长记录、零字节、空消息、析构刷新、重启及多实例冲突、参数检查、创建失败后的恢复、定时刷新、关闭定时刷新、并发追加与滚动、长周期等待的析构唤醒和 Logger 接入，已注册到 CTest。
