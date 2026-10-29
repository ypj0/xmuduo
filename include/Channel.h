#pragma once

#include "noncopyable.h"
#include <cstdint>
#include <functional>
#include <utility>

class EventLoop;

class Channel : private noncopyable{
public:
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

    EventLoop& owner_;
    const int fd_;

    bool reading_ = false;
    bool writing_ = false;

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback errorCallback_;
    EventCallback closeCallback_;
};
