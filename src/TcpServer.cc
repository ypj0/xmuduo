#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include <exception>
#include <stdexcept>
#include <unordered_map>
#include <utility>

struct TcpServer::Impl : std::enable_shared_from_this<Impl> {
    EventLoop& loop;
    const Options options;
    std::unique_ptr<Acceptor> acceptor;
    InetAddress address;
    std::unordered_map<TcpConnection*, TcpConnection::Ptr> connections;
    TcpConnection::MessageCallback messageCallback;
    TcpConnection::CloseCallback closeCallback;
    std::function<void()> stoppedCallback;
    State state = State::ready;
    bool notify = true;

    Impl(EventLoop& owner, const InetAddress& bindAddress, Options config)
        : loop(owner), options(config), address(bindAddress) {
        loop.assertInLoopThread();
        if (options.maxConnections == 0 || options.bufferLimit == 0 || options.backlog <= 0) {
            throw std::invalid_argument("Server limits and backlog must be positive");
        }
        acceptor = std::make_unique<Acceptor>(loop, address, options.reusePort);
        address = acceptor->localAddress();
    }

    void checkConfiguration() const {
        loop.assertInLoopThread();
        if (state != State::ready) { throw std::logic_error("Configure callbacks before start"); }
    }

    void install() {
        const std::weak_ptr<Impl> weak = shared_from_this();
        acceptor->setNewConnectionCallback([weak](Socket socket, const InetAddress&) {
            if (auto self = weak.lock()) { self->accepted(std::move(socket)); }
        });
    }

    void accepted(Socket socket) {
        if (state != State::running || connections.size() >= options.maxConnections) {
            return; // RAII rejects the excess connection without registering a Channel.
        }
        const std::weak_ptr<Impl> weak = shared_from_this();
        auto connection = TcpConnection::create(loop, std::move(socket),
            [weak](const TcpConnection::Ptr& connection, Buffer& input) {
                if (auto self = weak.lock(); self && self->state == State::running && self->notify) {
                    self->messageCallback(connection, input);
                }
            },
            [weak](const TcpConnection::Ptr& connection, std::error_code error) {
                if (auto self = weak.lock()) { self->closed(connection, error); }
            }, options.bufferLimit);
        connections.emplace(connection.get(), connection);
    }

    void maybeStopped() {
        if (state == State::stopping && !acceptor && connections.empty()) {
            state = State::stopped;
            if (notify && stoppedCallback) { stoppedCallback(); }
        }
    }

    void closed(const TcpConnection::Ptr& connection, std::error_code error) {
        connections.erase(connection.get());
        // User callback failures must not prevent the server reaching stopped.
        std::exception_ptr failure;
        try { if (notify && closeCallback) { closeCallback(connection, error); } }
        catch (...) { failure = std::current_exception(); }
        try { maybeStopped(); }
        catch (...) { if (!failure) { failure = std::current_exception(); } }
        if (failure) { std::rethrow_exception(failure); }
    }

    void stop() {
        loop.assertInLoopThread();
        if (state == State::stopping || state == State::stopped) { return; }
        // Queue first so that a failed allocation doesn't falsely mark stopping.
        auto self = shared_from_this();
        loop.queueInLoop([self] {
            self->acceptor.reset(); // Task phase: safe to destroy the listener Channel.
            for (auto& [key, connection] : self->connections) {
                (void)key;
                connection->forceClose();
            }
            self->maybeStopped();
        });
        state = State::stopping;
        if (acceptor) { acceptor->stop(); }
    }
};

TcpServer::TcpServer(EventLoop& loop, const InetAddress& address)
    : TcpServer(loop, address, Options{}) {}

TcpServer::TcpServer(EventLoop& loop, const InetAddress& address, Options options)
    : impl_(std::make_shared<Impl>(loop, address, options)) { impl_->install(); }

TcpServer::~TcpServer() noexcept {
    try {
        impl_->loop.assertInLoopThread();
        if (impl_->loop.isDispatching()) { std::terminate(); }
        impl_->notify = false; // User captures may be destroyed alongside this wrapper.
        impl_->stop();
    } catch (...) { std::terminate(); }
}

void TcpServer::setMessageCallback(TcpConnection::MessageCallback callback) {
    impl_->checkConfiguration();
    if (!callback) { throw std::invalid_argument("Message callback must not be empty"); }
    impl_->messageCallback = std::move(callback);
}
void TcpServer::setCloseCallback(TcpConnection::CloseCallback callback) {
    impl_->checkConfiguration();
    impl_->closeCallback = std::move(callback);
}
void TcpServer::setStoppedCallback(std::function<void()> callback) {
    impl_->checkConfiguration();
    impl_->stoppedCallback = std::move(callback);
}
void TcpServer::start() {
    impl_->loop.assertInLoopThread();
    if (impl_->state == State::running) {
        impl_->acceptor->start(impl_->options.backlog); // Also resumes Acceptor after a caught error.
        return;
    }
    if (impl_->state != State::ready) { throw std::logic_error("Stopped server cannot restart"); }
    if (!impl_->messageCallback) { throw std::logic_error("Set a message callback before start"); }
    impl_->acceptor->start(impl_->options.backlog);
    impl_->state = State::running;
}
void TcpServer::stop() { impl_->stop(); }
TcpServer::State TcpServer::state() const {
    impl_->loop.assertInLoopThread();
    return impl_->state;
}
std::size_t TcpServer::connectionCount() const {
    impl_->loop.assertInLoopThread();
    return impl_->connections.size();
}
InetAddress TcpServer::localAddress() const {
    impl_->loop.assertInLoopThread();
    return impl_->address;
}
