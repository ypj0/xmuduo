#pragma once

#include "noncopyable.h"
#include <chrono>
#include <sys/epoll.h>
#include <unordered_map>
#include <Channel.h>
#include <vector>

class Channel;

class EPollPoller : private noncopyable {
public:
    struct ActiveEvent {
        Channel* channel;
        Channel::ReadyEvents events;
    };

    EPollPoller();
    ~EPollPoller() noexcept;

    void updateChannel(Channel& channel);
    void removeChannel(Channel& channel);
    void poll(std::chrono::milliseconds timeout,
              std::vector<ActiveEvent>& activeEvents);
private:
    std::vector<epoll_event> events_;
    int epollFd_ = -1;
    std::unordered_map<int, Channel*> channels_;
};
