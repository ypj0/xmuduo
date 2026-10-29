#pragma once

#include "FileSink.h"
#include "noncopyable.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

// Synchronous, record-preserving rotation with periodic stdio flushing.
class RollingFileSink : noncopyable {
public:
    RollingFileSink(std::filesystem::path directory,
                    std::string basename,
                    std::uintmax_t rollSize,
                    std::chrono::milliseconds flushInterval = std::chrono::seconds{3});

    void append(std::string_view message);
    void flush();

private:
    void roll();
    void periodicFlush(std::stop_token stopToken);
    void rethrowBackgroundError() const;

    std::filesystem::path directory_;   // 日志目录
    std::string basename_;              // 文件前缀名

    std::uintmax_t rollSize_;           // 滚动阈值
    std::chrono::milliseconds flushInterval_;
    std::uintmax_t writtenBytes_ = 0;   // 当前文件已成功追加的字节数
    std::uint64_t sequence_ = 0;        // 下一个候选文件序号，已有文件会跳过

    std::unique_ptr<FileSink> currentFile_; // 拥有当前输出文件
    std::mutex mutex_;                  // 同步调用与后台刷新
    std::condition_variable_any flushCondition_;
    std::exception_ptr backgroundError_;
    std::jthread flushThread_;           // 声明最后，析构时先停止并等待线程
};
