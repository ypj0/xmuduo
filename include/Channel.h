#pragma once

#include "noncopyable.h"
#include <cstdint>
#include <functional>
#include <utility>

class EventLoop;

class Channel : private noncopyable{
public:

    // 对应就绪事件
    struct ReadyEvents {
        bool readable = false;
        bool writable = false;
        bool peerClosed = false;
        bool hangup = false;
        bool error = false;
    };

    void handleEvent(ReadyEvents events);

    using EventCallback = std::function<void()>;

    Channel(EventLoop& owner, int fd);

    [[nodiscard]]
    int fd() const noexcept {
        return fd_;
    }

    [[nodiscard]]
    EventLoop& ownerLoop() const noexcept {
        return owner_;
    }

    void setReadCallback(EventCallback callback) {
        readCallback_ = std::move(callback);
    }

    void setWriteCallback(EventCallback callback) {
        writeCallback_ = std::move(callback);
    }

    void setErrorCallback(EventCallback callback) {
        errorCallback_ = std::move(callback);
    }

    void setCloseCallback(EventCallback callback) {
        closeCallback_ = std::move(callback);
    }

    [[nodiscard]]
    bool isReading() const noexcept {
        return reading_;
    }

    [[nodiscard]]
    bool isWriting() const noexcept {
        return writing_;
    }

    void enableReading();
    void disableReading();

    void enableWriting();
    void disableWriting();

    void disableAll();
    void remove();
private:
    void update();

    EventLoop& owner_;  // 所属的事件循环
    const int fd_;      // Channel绑定的fd

    // 意愿状态位，表达希望监听的事件，而不是事件注册的事件
    bool reading_ = false;
    bool writing_ = false;

    // 回调函数
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback errorCallback_;
    EventCallback closeCallback_;
};
