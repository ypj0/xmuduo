#pragma once

#include "TcpConnection.h"
#include <memory>

// Single-EventLoop server. All methods, including destruction, are loop-thread-only.
class TcpServer : noncopyable {
public:
    struct Options {
        std::size_t maxConnections = 1024;
        std::size_t bufferLimit = 64 * 1024 * 1024;
        int backlog = SOMAXCONN;
        bool reusePort = false;
    };
    enum class State { ready, running, stopping, stopped };
    TcpServer(EventLoop& loop, const InetAddress& address);
    TcpServer(EventLoop& loop, const InetAddress& address, Options options);
    ~TcpServer() noexcept;

    // Configure before start(). Callbacks remain fixed while serving.
    void setMessageCallback(TcpConnection::MessageCallback callback);
    void setCloseCallback(TcpConnection::CloseCallback callback);
    void setStoppedCallback(std::function<void()> callback);
    void start();
    void stop(); // Asynchronously force-close all connections; terminal, idempotent.
    [[nodiscard]] State state() const;
    [[nodiscard]] std::size_t connectionCount() const;
    [[nodiscard]] InetAddress localAddress() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
