#include "Buffer.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace {
std::size_t checkedInitial(std::size_t initial, std::size_t maximum) {
    if (maximum == 0 || initial > maximum) {
        throw std::invalid_argument("Buffer capacity must satisfy initial <= maximum and maximum > 0");
    }
    return initial;
}
}

Buffer::Buffer(std::size_t initialCapacity, std::size_t maxCapacity)
    : storage_(checkedInitial(initialCapacity, maxCapacity)), maxCapacity_(maxCapacity) {}

std::string_view Buffer::readableView() const noexcept {
    if (readableBytes() == 0) { return {}; }
    return {storage_.data() + reader_, readableBytes()};
}

void Buffer::ensureWritable(std::size_t bytes) {
    if (bytes <= writableBytes()) { return; }
    const auto readable = readableBytes();
    if (bytes > maxCapacity_ - readable) {
        throw std::length_error("Buffer maximum capacity exceeded");
    }
    if (bytes <= storage_.size() - readable) {
        if (readable != 0) {
            std::memmove(storage_.data(), storage_.data() + reader_, readable);
        }
    } else {
        const auto doubled = storage_.size() > maxCapacity_ / 2
            ? maxCapacity_ : storage_.size() * 2;
        std::vector<char> next(std::max(readable + bytes, doubled));
        if (readable != 0) {
            std::memcpy(next.data(), storage_.data() + reader_, readable);
        }
        storage_.swap(next);
    }
    reader_ = 0;
    writer_ = readable;
}

void Buffer::append(std::string_view bytes) {
    if (bytes.empty()) { return; }
    if (bytes.size() > maxCapacity_ - readableBytes()) {
        throw std::length_error("Buffer maximum capacity exceeded");
    }
    // A view into this buffer would otherwise be invalidated by compaction or
    // growth. Snapshot only this aliasing case, not normal external appends.
    std::string snapshot;
    const auto less = std::less<const char*>{};
    if (!storage_.empty() && !less(bytes.data(), storage_.data()) &&
        less(bytes.data(), storage_.data() + storage_.size())) {
        snapshot.assign(bytes);
        bytes = snapshot;
    }
    ensureWritable(bytes.size());
    std::memcpy(storage_.data() + writer_, bytes.data(), bytes.size());
    writer_ += bytes.size();
}

std::span<char> Buffer::writableSpan(std::size_t minimum) {
    ensureWritable(minimum);
    if (writableBytes() == 0) { return {}; }
    return {storage_.data() + writer_, writableBytes()};
}

void Buffer::commitWritten(std::size_t bytes) {
    if (bytes > writableBytes()) { throw std::out_of_range("Commit exceeds writable bytes"); }
    writer_ += bytes;
}

void Buffer::retrieve(std::size_t bytes) {
    if (bytes > readableBytes()) { throw std::out_of_range("Retrieve exceeds readable bytes"); }
    reader_ += bytes;
    if (reader_ == writer_) { retrieveAll(); }
}

void Buffer::retrieveAll() noexcept {
    reader_ = 0;
    writer_ = 0;
}

std::string Buffer::retrieveAsString(std::size_t bytes) {
    if (bytes > readableBytes()) { throw std::out_of_range("Retrieve exceeds readable bytes"); }
    std::string result{readableView().substr(0, bytes)};
    retrieve(bytes);
    return result;
}
