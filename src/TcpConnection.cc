#include "TcpConnection.h"
#include "EventLoop.h"
#include <algorithm>
#include <cerrno>
#include <exception>
#include <stdexcept>
#include <utility>

TcpConnection::Ptr TcpConnection::create(EventLoop& loop, Socket socket,
        MessageCallback message, CloseCallback close, std::size_t bufferLimit) {
    loop.assertInLoopThread();
    if (!message) { throw std::invalid_argument("Message callback must not be empty"); }
    auto deleter = [&loop](TcpConnection* connection) noexcept {
        try {
            if (loop.isInLoopThread() && !loop.isDispatching()) { delete connection; }
            else { loop.queueInLoop([connection] { delete connection; }); }
        } catch (...) { std::terminate(); }
    };
    Ptr connection{new TcpConnection(loop, std::move(socket), std::move(message),
                                   std::move(close), bufferLimit), deleter};
    connection->start(connection);
    return connection;
}

TcpConnection::TcpConnection(EventLoop& loop, Socket socket, MessageCallback message,
        CloseCallback close, std::size_t limit)
    : loop_(loop), socket_(std::move(socket)), channel_(loop, socket_->fd()),
      local_(socket_->localAddress()), peer_(socket_->peerAddress()),
      input_(std::min<std::size_t>(1024, limit), limit),
      output_(std::min<std::size_t>(1024, limit), limit),
      messageCallback_(std::move(message)), closeCallback_(std::move(close)) {}

TcpConnection::~TcpConnection() noexcept {
    try {
        loop_.assertInLoopThread();
        if (socket_) { channel_.disableAll(); channel_.remove(); }
    } catch (...) { std::terminate(); }
}

void TcpConnection::start(const Ptr& self) {
    const std::weak_ptr<TcpConnection> weak = self;
    channel_.setReadCallback([weak] { if (auto p = weak.lock()) { p->handleRead(); } });
    channel_.setWriteCallback([weak] { if (auto p = weak.lock()) { p->handleWrite(); } });
    channel_.setErrorCallback([weak] { if (auto p = weak.lock()) { p->handleError(); } });
    channel_.setCloseCallback([weak] { if (auto p = weak.lock()) { p->beginClose(); } });
    channel_.enableReading();
}

TcpConnection::State TcpConnection::state() const {
    loop_.assertInLoopThread();
    return state_;
}
std::size_t TcpConnection::pendingOutputBytes() const {
    loop_.assertInLoopThread();
    return output_.readableBytes();
}

void TcpConnection::send(std::string_view bytes) {
    loop_.assertInLoopThread();
    if (state_ != State::connected) { throw std::logic_error("Connection is not accepting sends"); }
    if (bytes.empty()) { return; }
    // Bound and allocate before any I/O: overflow rejects this entire append.
    output_.append(bytes);
    handleWrite();
}

void TcpConnection::handleRead() {
    if (state_ == State::closing || state_ == State::closed) { return; }
    const auto remaining = input_.maxCapacity() - input_.readableBytes();
    if (remaining == 0) { beginClose(std::make_error_code(std::errc::no_buffer_space)); return; }
    auto writable = input_.writableSpan(1);
    writable = writable.first(std::min<std::size_t>(writable.size(), 64 * 1024));
    ssize_t received;
    do { received = ::recv(socket_->fd(), writable.data(), writable.size(), 0); }
    while (received < 0 && errno == EINTR);
    if (received > 0) {
        input_.commitWritten(static_cast<std::size_t>(received));
        const auto self = shared_from_this();
        try { messageCallback_(self, input_); }
        catch (...) { beginClose(std::make_error_code(std::errc::operation_canceled)); throw; }
        return;
    }
    if (received == 0) {
        peerEof_ = true;
        state_ = State::disconnecting;
        channel_.disableReading();
        if (output_.readableBytes() == 0) { outputDrained(); }
        return;
    }
    const int error = errno;
    if (error != EAGAIN && error != EWOULDBLOCK) {
        beginClose({error, std::generic_category()});
    }
}

void TcpConnection::handleWrite() {
    if (state_ == State::closing || state_ == State::closed) { return; }
    std::size_t budget = 256 * 1024;
    while (output_.readableBytes() != 0 && budget != 0) {
        const auto bytes = output_.readableView();
        const auto count = std::min(bytes.size(), budget);
        const auto written = ::send(socket_->fd(), bytes.data(), count, MSG_NOSIGNAL);
        if (written > 0) {
            output_.retrieve(static_cast<std::size_t>(written));
            budget -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) { continue; }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { break; }
        const int error = written == 0 ? EIO : errno;
        beginClose({error, std::generic_category()});
        return;
    }
    if (output_.readableBytes() == 0) {
        channel_.disableWriting();
        outputDrained();
    } else {
        channel_.enableWriting();
    }
}

void TcpConnection::shutdown() {
    loop_.assertInLoopThread();
    if (state_ != State::connected) { return; }
    state_ = State::disconnecting;
    if (output_.readableBytes() == 0) { outputDrained(); }
}

void TcpConnection::outputDrained() {
    if (state_ != State::disconnecting) { return; }
    if (peerEof_) { beginClose(); return; }
    if (!writeShutdown_) {
        try { socket_->shutdownWrite(); writeShutdown_ = true; }
        catch (const std::system_error& error) { beginClose(error.code()); }
    }
}

void TcpConnection::handleError() {
    if (!socket_) { return; }
    int error = 0;
    socklen_t length = sizeof(error);
    if (::getsockopt(socket_->fd(), SOL_SOCKET, SO_ERROR, &error, &length) < 0) { error = errno; }
    if (error != 0) { beginClose({error, std::generic_category()}); }
}

void TcpConnection::forceClose() {
    loop_.assertInLoopThread();
    beginClose();
}

void TcpConnection::beginClose(std::error_code error) {
    if (state_ == State::closing || state_ == State::closed) { return; }
    auto self = shared_from_this();
    // Queue before changing state: an allocation failure must not leave a
    // permanently closing object without its cleanup task.
    loop_.queueInLoop([self, error] { self->finishClose(error); });
    state_ = State::closing;
    channel_.disableAll();
}

void TcpConnection::finishClose(std::error_code error) {
    if (state_ == State::closed) { return; }
    channel_.disableAll();
    channel_.remove();
    socket_.reset();
    state_ = State::closed;
    input_.retrieveAll();
    output_.retrieveAll();
    if (closeCallback_) {
        const auto self = shared_from_this();
        closeCallback_(self, error);
    }
}
