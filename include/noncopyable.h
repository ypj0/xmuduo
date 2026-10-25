#pragma once

namespace xmuduo {

// 作为基类使用，禁止派生类默认的拷贝操作
class noncopyable {
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};
} // namespace xmuduo