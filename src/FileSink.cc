#include "FileSink.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

void FileSink::FileCloser::operator()(std::FILE* file) const noexcept {
    if (file != nullptr) {
        (void)std::fclose(file);
    }
}

FileSink::FileSink(const std::filesystem::path& path, OpenMode mode) {
    if (mode != OpenMode::append && mode != OpenMode::createExclusive) {
        throw std::invalid_argument("Invalid file open mode");
    }
    const auto filename = path.string();
    if (filename.find('\0') != std::string::npos) {
        throw std::invalid_argument("Log file path contains a null character");
    }
    int flags = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC;
    if (mode == OpenMode::createExclusive) {
        flags |= O_EXCL;
    }
    const int fd = ::open(filename.c_str(), flags, 0644);
    if (fd < 0) {
        const int error = errno != 0 ? errno : EIO;
        throw std::system_error(error, std::generic_category(),
                                "failed to open log file " + filename);
    }

    auto* file = ::fdopen(fd, "ab");
    if (file == nullptr) {
        const int error = errno != 0 ? errno : EIO;
        (void)::close(fd);
        // Do not unlink by pathname here: another process may have replaced
        // the entry since open(). An exclusive-create failure can leave an
        // empty file, but must never delete someone else's replacement.
        throw std::system_error(error, std::generic_category(),
                                "failed to create stream for log file " + filename);
    }
    file_.reset(file);
}

void FileSink::append(std::string_view message) {
    if (message.empty()) {
        return;
    }

    const auto written = std::fwrite(message.data(), 1, message.size(), file_.get());
    if (written != message.size()) {
        const int error = errno != 0 ? errno : EIO;
        throw std::system_error(error, std::generic_category(),
                                "failed to append to log file");
    }
}

void FileSink::flush() {
    if (std::fflush(file_.get()) != 0) {
        const int error = errno != 0 ? errno : EIO;
        throw std::system_error(error, std::generic_category(),
                                "failed to flush log file");
    }
}
