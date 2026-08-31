#pragma once

#include <unistd.h>

#include <cerrno>
#include <utility>

namespace RSCGroup {

class ScopedFd {
public:
    using CloseFunction = int (*)(int);

    ScopedFd() noexcept = default;

    explicit ScopedFd(int fd, CloseFunction closeFn = &::close) noexcept
        : fd_(fd)
        , closeFn_(closeFn)
    {
    }

    ~ScopedFd()
    {
        reset();
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept
        : fd_(other.release())
        , closeFn_(other.closeFn_)
    {
    }

    ScopedFd& operator=(ScopedFd&& other) noexcept
    {
        if (this != &other) {
            reset();
            fd_ = other.release();
            closeFn_ = other.closeFn_;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] int release() noexcept
    {
        const int released = fd_;
        fd_ = -1;
        return released;
    }

    void reset(int fd = -1, CloseFunction closeFn = &::close) noexcept
    {
        if (fd_ >= 0) {
            const int savedErrno = errno;
            closeFn_(fd_);
            errno = savedErrno;
        }
        fd_ = fd;
        closeFn_ = closeFn;
    }

private:
    int fd_{-1};
    CloseFunction closeFn_{&::close};
};

} // namespace RSCGroup
