#include "Channel.h"
#include "EventLoop.h"
#include <stdexcept>

Channel::Channel(EventLoop& owner, int fd)
    : owner_(owner), fd_(fd)
{
    if(fd < 0) {
        throw std::invalid_argument("Channel fd must be non-negative");
    }
}

// 委托给事件循环
void Channel::update() {
    owner_.updateChannel(*this);
}

void Channel::remove() {
    owner_.removeChannel(*this);
}

// 处理事件分发
void Channel::handleEvent(ReadyEvents events) {
    owner_.assertInLoopThread();
    // Invoke stored functions so mutable captures retain state between events.
    // Replacing an executing callback is forbidden by the lifecycle contract.
    const auto& close = closeCallback_;
    const auto& error = errorCallback_;
    const auto& read = readCallback_;
    const auto& write = writeCallback_;
    if (events.hangup && !events.readable && !events.peerClosed && close) { close(); }
    if (events.error && (reading_ || writing_) && error) { error(); }
    if ((events.readable || events.peerClosed) && reading_ && read) { read(); }
    if (events.writable && writing_ && write) { write(); }
}

void Channel::enableReading() {
    owner_.assertInLoopThread();

    if(reading_) {
        return;
    }

    reading_ = true;

    try {
        update();
    } catch(...) {
        reading_ = false;
        throw;
    }
}

void Channel::disableReading() {
    owner_.assertInLoopThread();

    if(!reading_) {
        return;
    }

    reading_ = false;

    try {
        update();
    } catch(...) {
        reading_ = true;
        throw ;
    }
}

void Channel::enableWriting() {
    owner_.assertInLoopThread();

    if(writing_) {
        return;
    }

    writing_ = true;

    try {
        update();
    } catch(...) {
        writing_ = false;
        throw;
    }
}

void Channel::disableWriting() {
    owner_.assertInLoopThread();

    if(!writing_) {
        return;
    }

    writing_ = false;

    try {
        update();
    } catch(...) {
        writing_= true;
        throw ;
    }
}

void Channel::disableAll() {
    owner_.assertInLoopThread();

    if(!reading_ && !writing_) {
        return;
    }

    const bool oldReading = reading_;
    const bool oldWriting = writing_;

    reading_ = false;
    writing_ = false;

    try {
        update();
    } catch(...) {
        reading_ = oldReading;
        writing_ = oldWriting;
        throw;
    }
}
