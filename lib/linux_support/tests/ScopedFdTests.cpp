#include <ScopedFd.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

namespace {

using RSCGroup::ScopedFd;

int g_closeCount = 0;
int g_lastClosedFd = -1;

int countingClose(int fd)
{
    ++g_closeCount;
    g_lastClosedFd = fd;
    return 0;
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testMoveTransfersOwnership()
{
    g_closeCount = 0;
    g_lastClosedFd = -1;

    ScopedFd first(42, &countingClose);
    ScopedFd second(std::move(first));
    expect(!first.valid(), "moved-from fd should be invalid");
    expect(second.get() == 42, "move constructor should transfer fd");

    ScopedFd third;
    third = std::move(second);
    expect(!second.valid(), "move assignment should invalidate source");
    expect(third.get() == 42, "move assignment should transfer fd");
}

void testReleaseAndResetAreIdempotent()
{
    g_closeCount = 0;
    g_lastClosedFd = -1;

    ScopedFd fd(77, &countingClose);
    expect(fd.release() == 77, "release should return owned fd");
    expect(!fd.valid(), "released fd should become invalid");
    fd.reset();
    expect(g_closeCount == 0, "reset on invalid fd should not close");

    fd.reset(88, &countingClose);
    fd.reset();
    fd.reset();
    expect(g_closeCount == 1, "reset should close exactly once");
    expect(g_lastClosedFd == 88, "reset should close the owned fd");
}

void testDestructorClosesExactlyOnce()
{
    g_closeCount = 0;
    g_lastClosedFd = -1;
    {
        ScopedFd fd(99, &countingClose);
        expect(fd.valid(), "fd should start valid");
    }
    expect(g_closeCount == 1, "destructor should close exactly once");
    expect(g_lastClosedFd == 99, "destructor should close the correct fd");
}

void testRealEventFdCloses()
{
    const int rawFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    expect(rawFd >= 0, "eventfd creation failed");

    ScopedFd fd(rawFd);
    const int released = fd.release();
    expect(released == rawFd, "release should return the real eventfd");
    expect(::close(released) == 0, "released eventfd should still be closeable");
}

} // namespace

int main()
{
    testMoveTransfersOwnership();
    testReleaseAndResetAreIdempotent();
    testDestructorClosesExactlyOnce();
    testRealEventFdCloses();
    return EXIT_SUCCESS;
}
