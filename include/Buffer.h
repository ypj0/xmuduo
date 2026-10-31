#pragma once

#include "noncopyable.h"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Connection-owned byte buffer. All access belongs to its EventLoop thread.
class Buffer : noncopyable {
public:
    explicit Buffer(std::size_t initialCapacity = 1024,
                    std::size_t maxCapacity = 64 * 1024 * 1024);

    [[nodiscard]] std::size_t readableBytes() const noexcept { return writer_ - reader_; }
    [[nodiscard]] std::size_t writableBytes() const noexcept { return storage_.size() - writer_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] std::size_t maxCapacity() const noexcept { return maxCapacity_; }
    [[nodiscard]] std::string_view readableView() const noexcept;

    void append(std::string_view bytes);
    void ensureWritable(std::size_t bytes);
    [[nodiscard]] std::span<char> writableSpan(std::size_t minimum = 1);
    void commitWritten(std::size_t bytes);

    void retrieve(std::size_t bytes);
    void retrieveAll() noexcept;
    [[nodiscard]] std::string retrieveAsString(std::size_t bytes);

private:
    std::vector<char> storage_;
    const std::size_t maxCapacity_;
    std::size_t reader_ = 0;
    std::size_t writer_ = 0;
};
