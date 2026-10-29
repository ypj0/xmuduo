# Logger

公开接口位于 `include/Logger.h`，实现位于 `src/Logger.cc`，使用全局命名空间。当前为同步日志实现。

## 等级管理

`setLevel()` 设置进程阈值，`level()` 读取阈值，`enable()` 判断消息是否开启。默认阈值为 `info`，内部使用 relaxed 原子读写。等级定义见 [LogLevel](loglevel.md)。

## 输出配置

`setOutput(OutputFunc, FlushFunc)` 同时安装两个 `std::function` 回调，支持捕获状态的 lambda；空回调抛出 `std::invalid_argument`。默认使用标准输出。

`write(std::string_view)` 原样提交字节，`flush()` 请求刷新。回调替换、调用与刷新使用同一互斥锁保护。回调不得重入这三个接口，也不得保存借用的消息视图供返回后使用；异步输出端必须复制字节。回调异常向上传播。默认刷新使用 `fflush`，不保证磁盘持久化。

## 日志入口

`log(level, message, location)` 组装 `[UTC时间] [等级] 正文 - 文件:行号\n`，默认位置来自调用处的 `std::source_location`。

`logf(level, location, format, args...)` 先在 [LogBuffer](logbuffer.md) 中格式化正文，再调用 `log()`，须显式传递源码位置。

二者被过滤时返回 true，容量不足时返回 false 且不输出残缺记录，成功提交时返回 true。格式化和输出异常向上传播。完整记录容量为 4096 字节，包含前缀、正文和后缀。正文中的换行不转义，因此一次提交可能包含多行文本。

## 普通日志宏

```cpp
#include "Logger.h"

Logger::setLevel(LogLevel::info);
LOG_INFO("server started");
LOG_WARN("connection fd={}", 42);
LOG_ERROR("request failed: code={}", 7);
```

提供 `LOG_TRACE`、`LOG_DEBUG`、`LOG_INFO`、`LOG_WARN`、`LOG_ERROR`，共用内部宏以保持行为一致。关闭等级时不求值参数；源码位置来自宏调用处。宏采用 `do/while(false)`，可用于不加花括号的 if/else。宏仍在编译期检查已关闭等级的格式字符串。

宏在容量不足时抛出 `std::length_error`，其他异常继续传播。当前不提供致命日志宏；直接传入 fatal 等级也不会终止进程。

当前 logf 使用正文与完整记录两个缓冲区，存在正文复制及重复等级检查；后续将优化为单缓冲区。文件输出、滚动与异步后端尚未实现。

构建与测试方式见 [构建与测试](build.md)。`tests/Logger_test.cc` 覆盖等级配置、源码位置、格式化、记录容量、输出异常、宏过滤及 if/else 行为。
