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
    // 返回给上层的就绪事件列表
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
    std::vector<epoll_event> events_;   // epoll_wait的接收缓冲区
    int epollFd_ = -1;  // epoll实例fd，RAII
    std::unordered_map<int, Channel*> channels_;
    // fd->channel的映射，记录fd和感兴趣事件，为undateChannel提供一个可查询的API接口
};
