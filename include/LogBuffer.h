#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <format>
#include <utility>

template <std::size_t Capacity = 4096>
class LogBuffer {
public:
    static_assert(Capacity > 0, "LogBuffer capacity must be greater than 0");
    // 有效字节数
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
    // 模板参数容量
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }
    // 判断有效字节数是否为0
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }
    // 返回剩余容量
    [[nodiscard]] std::size_t available() const noexcept {
        return Capacity - size_;
    }
    // 返回数组的起始地址
    [[nodiscard]] const char* data() const noexcept {
        return storage_.data();
    }
    // 用起始地址加有效长度构造view视图
    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(storage_.data(), size_);
    }
    
    [[nodiscard]] bool append(std::string_view text) noexcept {
        if(text.size() > available()) return false;
        if(!text.empty()) {
            std::memcpy(storage_.data() + size_, text.data(), text.size());
        }
        size_ += text.size();
        return true;
    }

    template<typename... Args>
    [[nodiscard]]
    bool appendFormat(std::format_string<Args...> format, Args&&... args) {
        const auto remaining = available();
        const auto result = std::format_to_n(
            storage_.data() + size_, remaining,
            format, std::forward<Args>(args)...);
        // 仅在完整结果能够放下时提交长度；失败或异常不改变有效内容。
        if (static_cast<std::size_t>(result.size) > remaining) {
            return false;
        }
        size_ += static_cast<std::size_t>(result.size);
        return true;
    }
    // 只把有效长度设为0
    void reset() noexcept {
        size_ = 0;
    }

private:
    // 不加{}是为了避免默认构造时清零整个数组，只读取已写入的有效区域，其余字节不需要初始化
    std::array<char, Capacity> storage_;
    std::size_t size_ = 0;
};
