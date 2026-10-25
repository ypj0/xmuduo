# `Logger` 类设计文档

## 1. 设计目标

`xmuduo::Logger` 提供进程内统一的日志输出入口，包含四种消息等级：普通信息、错误、致命错误和调试信息。日志消息输出到标准输出，每条消息占一行，并带 UTC 时间戳。

当前设计聚焦于单进程内的简单同步输出：Logger 是单例，不允许复制；不同线程的消息由互斥锁保护，避免一条消息的时间戳、等级前缀和正文彼此交错。当前**没有文件输出、日志过滤、异步写入或日志滚动功能**。

## 2. 类型与接口

等级使用强类型枚举，避免与全局宏或普通整数混淆：

```cpp
enum class LogLevel {
    Info,
    Error,
    Fatal,
    Debug,
};

class Logger : private noncopyable {
public:
    static Logger& instance();
    void log(LogLevel level, std::string_view msg);
    void logf(LogLevel level, const char* format, ...);

private:
    Logger() = default;
    std::mutex mutex_;
};
```

`log` 接收已形成的消息视图；`logf` 接收 printf 风格的格式字符串和可变参数。Logger 不保存当前日志等级：等级是每条消息自己的参数，因此多个线程不会通过修改共享 `loglevel_` 互相覆盖等级。

## 3. 单例的创建流程

```cpp
Logger& Logger::instance() {
    static Logger logger;
    return logger;
}
```

调用流程如下：

1. 第一次调用 `instance()` 时，函数局部静态对象 `logger` 被构造。
2. 构造 Logger 时，其 `noncopyable` 基类子对象先构造，然后初始化 Logger 成员。
3. 返回该对象的引用。
4. 后续调用返回同一个对象。

C++11 起，函数局部静态对象的首次初始化是线程安全的。此保证只针对单例初始化；消息输出的同步由 Logger 自己的互斥锁负责。

Logger 的构造函数是私有的，因此调用者不能直接创建其他 Logger 实例。它私有继承 `noncopyable`，进一步禁止默认拷贝和赋值：调用者可以保存引用，但不能复制该单例。

## 4. 输出与线程安全

`log` 根据等级选择 `[INFO]`、`[ERROR]`、`[FATAL]` 或 `[DEBUG]` 前缀。在持有 `mutex_` 的情况下获取当前时间，再输出时间戳、等级、消息和换行符。时间戳由 `Timestamp::now().toString()` 生成，格式为 `YYYY-MM-DD HH:MM:SS.ffffff UTC`。时间在输出锁内取得，使时间戳顺序与 Logger 的消息提交顺序一致。锁覆盖整条输出，避免两个使用 Logger 的线程交错打印一行中的内容。

致命日志输出后会刷新 `std::cout`。宏随后调用 `std::exit(EXIT_FAILURE)`，以失败状态终止进程。致命日志不是 core dump；若未来需要崩溃转储或栈回溯，需要单独设计终止策略。

互斥锁只协调通过这个 Logger 输出的消息，不会自动协调其他代码直接写 `std::cout` 的输出。

## 5. 格式化流程

`logf` 使用 `std::vsnprintf` 格式化消息：

1. 空格式指针会输出格式错误提示。
2. 先用 1024 字节栈缓冲区尝试格式化。
3. 如果目标消息较短，直接将有效字符交给 `log`。
4. 如果缓冲区不够，则按 `vsnprintf` 返回的所需长度分配动态缓冲区，再次格式化。
5. 格式化错误会输出错误提示；较长消息会扩容，不会因为固定缓冲区而静默截断。

格式字符串必须与后续参数的类型匹配，这是 printf 风格可变参数接口的调用者责任。错误的格式说明符可能造成未定义行为。若以后希望获得编译器更强的类型检查，可考虑用 C++20 `std::format`（须确认目标工具链支持情况）或类型安全的格式化库替换该接口。

## 6. 日志宏

头文件提供 `LOG_INFO`、`LOG_ERROR`、`LOG_FATAL` 和 `LOG_DEBUG`。每个宏固定传入对应等级，并调用 `Logger::instance().logf(...)`。使用全限定名 `::xmuduo::...`，所以调用方不需要 `using namespace xmuduo`。

```cpp
LOG_INFO("server started");
LOG_ERROR("accept failed: %d", error_code);
LOG_DEBUG("connection id=%d", connection_id);
```

宏采用 `do { ... } while (0)` 结构，使其在 `if/else` 等语句位置表现为单条语句。`__VA_OPT__` 用于在有可变参数时加入逗号，因此无格式参数的调用和带格式参数的调用都可工作；这依赖 C++20。

`LOG_DEBUG` 由 CMake 选项 `XMUDUO_ENABLE_DEBUG_LOG` 控制，默认关闭：

```bash
cmake -S . -B build -DXMUDUO_ENABLE_DEBUG_LOG=ON
cmake --build build
```

关闭时，`LOG_DEBUG(...)` 展开为空操作，参数表达式不会求值。不要再用通用的 `DEBUG` 预处理宏控制此功能，因为它容易与调试等级名称冲突。

`LOG_FATAL` 输出并刷新日志后以 `EXIT_FAILURE` 退出当前进程，因此调用后代码不会继续执行。不要在仍需执行清理、栈展开或局部对象析构的路径上使用它；此宏不是异常处理机制。

## 7. 使用示例

```cpp
#include "logger.h"

int main() {
    LOG_INFO("starting");
    LOG_ERROR("request failed: code=%d", 7);

    xmuduo::Logger::instance().log(
        xmuduo::LogLevel::Info,
        "direct message");
}
```

典型输出：

```text
[2026-10-25 14:02:31.365338 UTC] [INFO] starting
[2026-10-25 14:02:31.365401 UTC] [ERROR] request failed: code=7
[2026-10-25 14:02:31.365452 UTC] [INFO] direct message
```

头文件位于 `include/logger.h`，实现位于 `src/logger.cc`。项目的 CMake target 将 `include/` 作为公开头文件搜索目录，并通过 `Threads::Threads` 链接线程支持。

## 8. 当前边界与后续方向

- 时间戳使用 `system_clock`，是 UTC 日历时间；系统校时可能使相邻日志时间前跳或后退。测量持续时间和实现定时器应使用单调时钟。
- 当前所有等级都会输出，没有运行时最低等级过滤。
- 当前同步写 `std::cout`，高并发时调用者会在互斥锁处等待。
- 当前不处理敏感信息脱敏，也不保证标准输出目的地可持久保存。
- `logf` 是 printf 风格接口，格式串和参数类型不匹配由调用者负责避免。

实现位于 [`include/logger.h`](../include/logger.h) 与 [`src/logger.cc`](../src/logger.cc)。构建选项定义在 [`CMakeLists.txt`](../CMakeLists.txt)。
