//
// Created by vvass on 05-Sep-26.
//
#include "NetlinkIo.h"

#include <EventFdSignal.h>
#include <UniqueFd.h>

#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace RSCGroup;

constexpr auto testTimeout = std::chrono::seconds(5);

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr
            << "NetlinkIoTests: "
            << message
            << '\n';

        std::exit(EXIT_FAILURE);
    }
}

struct SocketPair {
    UniqueFd tested;
    UniqueFd peer;
};

[[nodiscard]] SocketPair makeSocketPair(int type)
{
    int descriptors[2]{-1, -1};

    const int result = ::socketpair(
        AF_UNIX,
        type | SOCK_CLOEXEC,
        0,
        descriptors);

    if (result < 0) {
        std::cerr
            << "NetlinkIoTests: socketpair failed: "
            << std::strerror(errno)
            << '\n';

        std::exit(EXIT_FAILURE);
    }

    return {
        UniqueFd(descriptors[0]),
        UniqueFd(descriptors[1]),
    };
}

void sendDatagram(
    int fd,
    std::span<const char> payload)
{
    const ssize_t sent = ::send(
        fd,
        payload.data(),
        payload.size(),
        0);

    expect(
        sent == static_cast<ssize_t>(payload.size()),
        "failed to send complete test datagram");
}

void sendDatagram(
    int fd,
    std::string_view payload)
{
    sendDatagram(
        fd,
        std::span<const char>{
            payload.data(),
            payload.size(),
        });
}

[[nodiscard]] bool isReadable(int fd)
{
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLIN;

    const int result = ::poll(
        &descriptor,
        1,
        0);

    if (result < 0) {
        std::cerr
            << "NetlinkIoTests: poll failed: "
            << std::strerror(errno)
            << '\n';

        std::exit(EXIT_FAILURE);
    }

    return result > 0 &&
           (descriptor.revents & POLLIN) != 0;
}

void testBorrowedSocketDoesNotCloseDescriptor()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);

    const int borrowedFd = pair.tested.get();

    {
        NetlinkRouteSocket socket(borrowedFd);

        expect(
            socket.fd() == borrowedFd,
            "borrowed socket should expose the supplied descriptor");

        expect(
            !socket.ownsDescriptor(),
            "borrowed socket must not report descriptor ownership");
    }

    /*
     * The wrapper has been destroyed. Successful traffic proves that it did
     * not close the borrowed descriptor.
     */
    constexpr std::string_view payload{"borrowed-still-open"};

    sendDatagram(pair.peer.get(), payload);

    std::array<char, 64> buffer{};

    const NetlinkReceiveResult result =
        receiveNetlinkDatagram(
            pair.tested.get(),
            buffer);

    expect(
        result.status == NetlinkReceiveStatus::received,
        "borrowed descriptor should remain usable after wrapper destruction");

    expect(
        result.size == payload.size(),
        "borrowed descriptor should receive the complete datagram");

    expect(
        std::string_view(buffer.data(), result.size) == payload,
        "borrowed descriptor should receive the expected payload");
}

void testBorrowedSocketMovePreservesDescriptor()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);

    const int borrowedFd = pair.tested.get();

    {
        NetlinkRouteSocket original(borrowedFd);
        NetlinkRouteSocket moved(std::move(original));

        expect(
            moved.fd() == borrowedFd,
            "move construction should preserve the borrowed descriptor");

        expect(
            !moved.ownsDescriptor(),
            "moving a borrowed socket must not create ownership");
    }

    constexpr std::string_view payload{"still-borrowed"};

    sendDatagram(pair.peer.get(), payload);

    std::array<char, 32> buffer{};

    const auto result =
        receiveNetlinkDatagram(
            pair.tested.get(),
            buffer);

    expect(
        result.received(),
        "moved borrowed wrapper must not close the descriptor");
}

void testInvalidBorrowedDescriptorIsRejected()
{
    bool threw = false;

    try {
        NetlinkRouteSocket socket(-1);
        (void)socket;
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect(
        threw,
        "negative borrowed descriptor should throw invalid_argument");
}

void testWaitReturnsDataReady()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);
    EventFdSignal stopSignal;

    constexpr std::string_view payload{"ready"};

    sendDatagram(pair.peer.get(), payload);

    const NetlinkWaitResult result =
        waitForNetlinkDataOrStop(
            pair.tested.get(),
            stopSignal);

    expect(
        result.status == NetlinkWaitStatus::data_ready,
        "readable data descriptor should return data_ready");

    expect(
        result.error == 0,
        "data_ready should not carry an error");

    expect(
        !isReadable(stopSignal.fd()),
        "data readiness must not alter the stop signal");

    std::array<char, 32> buffer{};

    const auto receiveResult =
        receiveNetlinkDatagram(
            pair.tested.get(),
            buffer);

    expect(
        receiveResult.received(),
        "queued data should remain available after wait returns");
}

void testWaitReturnsStoppedAndDrainsSignal()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);
    EventFdSignal stopSignal;

    auto waiter = std::async(
        std::launch::async,
        [&] {
            return waitForNetlinkDataOrStop(
                pair.tested.get(),
                stopSignal);
        });

    const int signalError = stopSignal.signal();

    expect(
        signalError == 0,
        "failed to signal stop eventfd");

    expect(
        waiter.wait_for(testTimeout) ==
            std::future_status::ready,
        "waitForNetlinkDataOrStop did not wake after stop signal");

    const NetlinkWaitResult result = waiter.get();

    expect(
        result.status == NetlinkWaitStatus::stopped,
        "stop signal should return stopped");

    expect(
        result.error == 0,
        "ordinary stop should not carry an error");

    expect(
        !isReadable(stopSignal.fd()),
        "wait helper should drain the stop signal");
}

void testStopTakesPriorityOverData()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);
    EventFdSignal stopSignal;

    constexpr std::string_view payload{"queued-data"};

    sendDatagram(pair.peer.get(), payload);

    const int signalError = stopSignal.signal();

    expect(
        signalError == 0,
        "failed to signal stop eventfd");

    const NetlinkWaitResult result =
        waitForNetlinkDataOrStop(
            pair.tested.get(),
            stopSignal);

    expect(
        result.status == NetlinkWaitStatus::stopped,
        "stop should take priority when both descriptors are readable");

    /*
     * Stop priority must not consume data from the data descriptor.
     */
    expect(
        isReadable(pair.tested.get()),
        "queued data should remain after stop takes priority");

    expect(
        !isReadable(stopSignal.fd()),
        "prioritized stop signal should be drained");
}

void testInvalidDataDescriptorReportsFailure()
{
    EventFdSignal stopSignal;

    const NetlinkWaitResult result =
        waitForNetlinkDataOrStop(
            -1,
            stopSignal);

        expect(
            result.status == NetlinkWaitStatus::data_fd_failed,
            "negative descriptor should return data_fd_failed");

        expect(
            result.error == EBADF,
            "negative descriptor should report EBADF");
}

void testClosedDataDescriptorReportsPollFailure()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);
    EventFdSignal stopSignal;

    const int closedFd = pair.tested.release();

    expect(
        ::close(closedFd) == 0,
        "failed to close test data descriptor");

    const NetlinkWaitResult result =
        waitForNetlinkDataOrStop(
            closedFd,
            stopSignal);

    expect(
        result.status == NetlinkWaitStatus::data_fd_failed,
        "POLLNVAL data descriptor should return data_fd_failed");

    expect(
        result.error == EIO,
        "data descriptor poll failure should report EIO");
}

void testReceiveCompleteDatagram()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);

    constexpr std::string_view payload{
        "complete-netlink-test-datagram"};

    sendDatagram(pair.peer.get(), payload);

    std::array<char, 128> buffer{};

    const NetlinkReceiveResult result =
        receiveNetlinkDatagram(
            pair.tested.get(),
            buffer);

    expect(
        result.status == NetlinkReceiveStatus::received,
        "complete datagram should return received");

    expect(
        result.size == payload.size(),
        "received size should match datagram size");

    expect(
        result.error == 0,
        "successful receive should not carry an error");

    expect(
        std::string_view(buffer.data(), result.size) == payload,
        "received datagram contents should match");
}

void testReceiveReportsTruncation()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);

    std::vector<char> payload(1024, 'x');

    sendDatagram(
        pair.peer.get(),
        std::span<const char>{
            payload.data(),
            payload.size(),
        });

    std::array<char, 16> buffer{};

    const NetlinkReceiveResult result =
        receiveNetlinkDatagram(
            pair.tested.get(),
            buffer);

    expect(
        result.status == NetlinkReceiveStatus::truncated,
        "oversized datagram should return truncated");

    expect(
        result.size == 0,
        "truncated datagram should not expose a usable size");

    expect(
        result.error == EMSGSIZE,
        "truncated datagram should report EMSGSIZE");
}

void testReceiveReportsClosedSocket()
{
    /*
     * SOCK_STREAM provides EOF when the peer closes. A connected datagram
     * socket does not generally report peer closure as a zero-length receive.
     */
    SocketPair pair = makeSocketPair(SOCK_STREAM);

    pair.peer.reset();

    std::array<char, 32> buffer{};

    const NetlinkReceiveResult result =
        receiveNetlinkDatagram(
            pair.tested.get(),
            buffer);

    expect(
        result.status == NetlinkReceiveStatus::closed,
        "stream EOF should return closed");

    expect(
        result.size == 0,
        "closed socket should return zero size");

    expect(
        result.error == ECONNRESET,
        "closed socket should report ECONNRESET");
}

void testReceiveRejectsEmptyBuffer()
{
    SocketPair pair = makeSocketPair(SOCK_DGRAM);

    const NetlinkReceiveResult result =
        receiveNetlinkDatagram(
            pair.tested.get(),
            std::span<char>{});

    expect(
        result.status == NetlinkReceiveStatus::failed,
        "empty buffer should return failed");

    expect(
        result.size == 0,
        "empty-buffer failure should return zero size");

    expect(
        result.error == EINVAL,
        "empty buffer should report EINVAL");
}

void testReceiveReportsInvalidDescriptor()
{
    std::array<char, 32> buffer{};

    const NetlinkReceiveResult result =
        receiveNetlinkDatagram(
            -1,
            buffer);

    expect(
        result.status == NetlinkReceiveStatus::failed,
        "invalid descriptor should return failed");

    expect(
        result.size == 0,
        "invalid descriptor should return zero size");

    expect(
        result.error == EBADF,
        "invalid descriptor should report EBADF");
}

} // namespace

int main()
{
    testBorrowedSocketDoesNotCloseDescriptor();
    testBorrowedSocketMovePreservesDescriptor();
    testInvalidBorrowedDescriptorIsRejected();

    testWaitReturnsDataReady();
    testWaitReturnsStoppedAndDrainsSignal();
    testStopTakesPriorityOverData();
    testInvalidDataDescriptorReportsFailure();
    testClosedDataDescriptorReportsPollFailure();

    testReceiveCompleteDatagram();
    testReceiveReportsTruncation();
    testReceiveReportsClosedSocket();
    testReceiveRejectsEmptyBuffer();
    testReceiveReportsInvalidDescriptor();

    return EXIT_SUCCESS;
}
