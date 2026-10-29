#pragma once

#include "noncopyable.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>

// Buffered append-only file output. Calls must be externally serialized.
class FileSink : noncopyable {
public:
    enum class OpenMode {
        append,
        createExclusive,
    };

    explicit FileSink(const std::filesystem::path& path,
                      OpenMode mode = OpenMode::append);

    void append(std::string_view message);
    void flush();

private:
    struct FileCloser {
        void operator()(std::FILE* file) const noexcept;
    };

    std::unique_ptr<std::FILE, FileCloser> file_;
};
