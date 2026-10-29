#include "EPollPoller.h"
#include "Channel.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <limits>
#include <algorithm>

#include <sys/epoll.h>
#include <unistd.h>

namespace {
    epoll_event makeEvent(Channel& channel) {
        epoll_event event{};

        if(channel.isReading()) {
            event.events |= EPOLLIN | EPOLLRDHUP;
        }

        if(channel.isWriting()) {
            event.events |= EPOLLOUT;
        }

        event.data.ptr = &channel;

        return event;
    }
}

EPollPoller::EPollPoller()
    : events_(16), epollFd_(::epoll_create1(EPOLL_CLOEXEC))
{
    if(epollFd_ < 0) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(), "epoll_create1");
    }
}

void EPollPoller::poll(std::chrono::milliseconds timeout,
                       std::vector<ActiveEvent>& activeEvents) {
    activeEvents.clear();
    if (timeout.count() < 0 || timeout.count() > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("Poll timeout must fit a non-negative int in milliseconds");
    }
    // Allocate before waiting; no allocation is required to retain returned events.
    const auto desired = std::max<std::size_t>(16, channels_.size());
    if (desired > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("Too many registered channels");
    }
    if (events_.size() < desired) { events_.resize(desired); }
    activeEvents.reserve(events_.size());
    const int count = ::epoll_wait(epollFd_, events_.data(),
        static_cast<int>(events_.size()), static_cast<int>(timeout.count()));
    if (count < 0) {
        const int error = errno;
        if (error == EINTR) { return; }
        throw std::system_error(error, std::generic_category(), "epoll_wait");
    }
    for (int i = 0; i < count; ++i) {
        const auto flags = events_[i].events;
        activeEvents.push_back({static_cast<Channel*>(events_[i].data.ptr), {
            .readable = (flags & EPOLLIN) != 0,
            .writable = (flags & EPOLLOUT) != 0,
            .peerClosed = (flags & EPOLLRDHUP) != 0,
            .hangup = (flags & EPOLLHUP) != 0,
            .error = (flags & EPOLLERR) != 0,
        }});
    }
}

EPollPoller::~EPollPoller() noexcept {
    const int saved = errno;
    if(epollFd_ >= 0) {
        (void)::close(epollFd_);
    }
    errno = saved;
}

void EPollPoller::updateChannel(Channel& channel) {
    const bool wantsEvents = channel.isReading() || channel.isWriting();

    auto it = channels_.find(channel.fd());

    if(it != channels_.end() && it->second != &channel) {
        throw std::logic_error("Another Channel already owns this registration");
    }

    if(it == channels_.end()) {
        if(!wantsEvents) {
            return;
        }
        // Allocate before ADD; roll back the map if the syscall fails.
        const auto inserted = channels_.emplace(channel.fd(), &channel).first;
        auto event = makeEvent(channel);
        if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, channel.fd(), &event) < 0) {
            const int error = errno;
            channels_.erase(inserted);
            throw std::system_error(error, std::generic_category(), "epoll_ctl ADD");
        }
        return;
    }
    if (!wantsEvents) {
        if (::epoll_ctl(epollFd_, EPOLL_CTL_DEL, channel.fd(), nullptr) < 0) {
            const int error = errno;
            throw std::system_error(error, std::generic_category(), "epoll_ctl DEL");
        }
        channels_.erase(it);
        return;
    }
    auto event = makeEvent(channel);
    if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, channel.fd(), &event) < 0) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(), "epoll_ctl MOD");
    }
}

void EPollPoller::removeChannel(Channel& channel) {
    if (channel.isReading() || channel.isWriting()) {
        throw std::logic_error("Disable all events before removing Channel");
    }
    if (channels_.contains(channel.fd())) {
        throw std::logic_error("Channel registration state is inconsistent");
    }
}
