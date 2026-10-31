#pragma once

#include "Buffer.h"
#include "Channel.h"
#include "Socket.h"
#include <functional>
#include <memory>
#include <system_error>

class EventLoop;

// Methods and callbacks run on the owning loop thread. EventLoop must outlive
// all connections and any queued deletion tasks.
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection> {
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using MessageCallback = std::function<void(const Ptr&, Buffer&)>;
    using CloseCallback = std::function<void(const Ptr&, std::error_code)>;
    enum class State { connected, disconnecting, closing, closed };

    [[nodiscard]] static Ptr create(EventLoop& loop, Socket socket,
        MessageCallback message, CloseCallback close = {},
        std::size_t bufferLimit = 64 * 1024 * 1024);

    void send(std::string_view bytes);
    void shutdown(); // Drain output, then SHUT_WR; keep reading until peer EOF.
    void forceClose();
    [[nodiscard]] State state() const;
    [[nodiscard]] std::size_t pendingOutputBytes() const;
    [[nodiscard]] const InetAddress& localAddress() const noexcept { return local_; }
    [[nodiscard]] const InetAddress& peerAddress() const noexcept { return peer_; }

private:
    TcpConnection(EventLoop& loop, Socket socket, MessageCallback message,
                  CloseCallback close, std::size_t limit);
    ~TcpConnection() noexcept;
    void start(const Ptr& self);
    void handleRead();
    void handleWrite();
    void handleError();
    void outputDrained();
    void beginClose(std::error_code error = {});
    void finishClose(std::error_code error);

    EventLoop& loop_;
    std::optional<Socket> socket_;
    Channel channel_;
    const InetAddress local_;
    const InetAddress peer_;
    Buffer input_;
    Buffer output_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    State state_ = State::connected;
    bool peerEof_ = false;
    bool writeShutdown_ = false;
};
