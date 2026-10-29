# FileSink

`FileSink` 是同步文件输出后端，声明在 `include/FileSink.h`，实现位于 `src/FileSink.cc`。私有继承项目的 `noncopyable`，不手写删除复制操作。

## 接口与所有权

- `FileSink(path, mode = OpenMode::append)`：默认追加打开，不存在则创建，已有内容保留。不会创建父目录。
- `OpenMode::createExclusive`：只允许创建新文件；路径已存在时抛出 `std::system_error`，错误码可与 `std::errc::file_exists` 比较，不修改已有文件。
- `append(message)`：按指针和长度写入，支持内嵌零字节，空消息不执行 I/O。不会自动添加换行。
- `flush()`：调用 `fflush`，将 stdio 缓冲内容提交给操作系统，不等同于 `fsync` 的持久化保证。

内部使用带自定义删除器的 `std::unique_ptr<FILE, FileCloser>` 独占句柄。编译器生成的析构函数自动销毁句柄，删除器调用 `fclose`，无需额外手写析构或重复关闭。析构不抛异常，关闭失败不报告；需要观察缓冲写入错误时，先显式调用 `flush()`。

打开、追加或刷新失败抛出 `std::system_error`。非法模式值或含内嵌零字符的路径抛出 `std::invalid_argument`。短写可能已提交部分字节，异常不意味着回滚；不要直接重试整条日志，否则可能重复前缀。

## 文件创建语义

实现面向 Linux/POSIX，先调用 `open`，再通过 `fdopen(fd, "ab")` 接入 stdio 缓冲。两种模式都使用 `O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC`；新文件权限为 `0644`，受进程 `umask` 限制。`O_CLOEXEC` 使描述符在成功执行 `exec` 时自动关闭。

独占创建额外使用 `O_EXCL`，由操作系统原子地检查并创建文件，避免“先检查是否存在，再打开”的竞争窗口。多个调用竞争创建同一路径时，只能有一个成功，其余收到 `EEXIST`。这里的独占仅指创建，不会阻止其他调用之后以追加模式打开文件。

```cpp
FileSink normal{"server.log"};
FileSink fresh{"server.0.log", FileSink::OpenMode::createExclusive};
```

若 `open` 成功但 `fdopen` 失败，实现保存错误码、关闭描述符并抛出异常，此时可能留下新建的空文件。不会按路径自动删除它，因为路径可能已被其他进程替换，删除会误伤无关文件。

## 接入 Logger

```cpp
#include "FileSink.h"
#include "Logger.h"
#include <memory>

const auto sink = std::make_shared<FileSink>("server.log");
Logger::setOutput(
    [sink](std::string_view message) { sink->append(message); },
    [sink] { sink->flush(); });

LOG_INFO("server started, port={}", 8080);
Logger::flush();
```

两个 lambda 按值捕获同一个 shared_ptr，保证回调存续期间文件对象有效。FileSink 不增加内部互斥锁；通过 Logger 调用时由 Logger 的输出锁串行化。直接共享调用时需要调用者同步。该组件尚不支持文件滚动或后台写入。

## 测试

`tests/FileSink_test.cc` 覆盖创建、顺序追加、零字节、重新打开、显式刷新、析构关闭、无效路径和 Logger 回调接入。另外验证独占创建成功、已有文件冲突且内容不变、非法模式及内嵌零字符路径，以及四个线程竞争创建同一文件时恰好一个成功。Linux 下利用 `/dev/full` 验证写入错误传播，测试使用临时目录并清理文件。
