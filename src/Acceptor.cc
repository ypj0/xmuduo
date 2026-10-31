#include "Acceptor.h"
#include "EventLoop.h"

#include <cerrno>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {
Socket makeSocket(EventLoop& loop, const InetAddress& address) {
    loop.assertInLoopThread();
    return Socket{address.family() == AF_INET6 ? Socket::Family::ipv6 : Socket::Family::ipv4};
}
bool transientAcceptError(int error) noexcept {
    // Linux can report queued per-connection network failures from accept4.
    switch (error) {
    case ECONNABORTED: case ENETDOWN: case EPROTO: case ENOPROTOOPT:
    case EHOSTDOWN: case ENONET: case EHOSTUNREACH: case EOPNOTSUPP:
    case ENETUNREACH: return true;
    default: return false;
    }
}
}

Acceptor::Acceptor(EventLoop& loop, const InetAddress& address, bool reusePort)
    : loop_(loop), socket_(makeSocket(loop, address)), channel_(loop, socket_.fd()) {
    socket_.setReuseAddress(true);
    socket_.setReusePort(reusePort);
    if (address.family() == AF_INET6) { socket_.setIPv6Only(true); }
    socket_.bind(address);
    channel_.setReadCallback([this] { handleRead(); });
}

Acceptor::~Acceptor() noexcept {
    // Never silently leave a dangling registration. Invalid lifetime usage or
    // failed unregistration is fatal; normal cleanup is outside I/O dispatch.
    try {
        stop();
        channel_.remove();
    } catch (...) { std::terminate(); }
}

void Acceptor::setNewConnectionCallback(NewConnectionCallback callback) {
    loop_.assertInLoopThread();
    if (handling_) { throw std::logic_error("Cannot replace callback while accepting"); }
    if (!callback) { throw std::invalid_argument("Connection callback must not be empty"); }
    callback_ = std::move(callback);
}

void Acceptor::start(int backlog) {
    loop_.assertInLoopThread();
    if (backlog <= 0) { throw std::invalid_argument("Listen backlog must be positive"); }
    if (!callback_) { throw std::logic_error("Set connection callback before start"); }
    if (channel_.isReading()) { return; }
    socket_.listen(backlog);
    channel_.enableReading();
}

void Acceptor::stop() {
    loop_.assertInLoopThread();
    channel_.disableAll();
}

bool Acceptor::accepting() const {
    loop_.assertInLoopThread();
    return channel_.isReading();
}

InetAddress Acceptor::localAddress() const {
    loop_.assertInLoopThread();
    return socket_.localAddress();
}

void Acceptor::handleRead() {
    loop_.assertInLoopThread();
    handling_ = true;
    try {
        // LT will notify us again if connections remain after this budget.
        for (int attempt = 0; attempt < 64 && channel_.isReading(); ++attempt) {
            std::optional<Socket> connection;
            try { connection = socket_.accept(); }
            catch (const std::system_error& error) {
                if (transientAcceptError(error.code().value())) { continue; }
                throw;
            }
            if (!connection) { break; }
            const auto peer = connection->peerAddress();
            callback_(std::move(*connection), peer);
        }
    } catch (...) {
        handling_ = false;
        stop(); // Pause on resource or callback failure instead of busy-spinning.
        throw;
    }
    handling_ = false;
}
