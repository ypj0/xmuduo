#pragma once

#include "Channel.h"
#include "Socket.h"
#include "noncopyable.h"
#include <functional>

class EventLoop;

// Loop-thread-only listener. Destroy outside I/O dispatch, before its EventLoop.
class Acceptor : noncopyable {
public:
    using NewConnectionCallback = std::function<void(Socket, const InetAddress&)>;
    Acceptor(EventLoop& loop, const InetAddress& address, bool reusePort = false);
    ~Acceptor() noexcept;

    void setNewConnectionCallback(NewConnectionCallback callback);
    void start(int backlog = SOMAXCONN);
    void stop(); // Pause accepting; the bound listening socket remains open.
    [[nodiscard]] bool accepting() const;
    [[nodiscard]] InetAddress localAddress() const;

private:
    void handleRead();
    EventLoop& loop_;
    Socket socket_;
    Channel channel_;
    NewConnectionCallback callback_;
    bool handling_ = false;
};
