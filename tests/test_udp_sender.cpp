// =============================================================================
// test_udp_sender.cpp
//
// Unit tests for UdpSender in src/udp_sender.cpp.
//
// HOW THE NETWORK IS TESTED WITHOUT A NETWORK
//   Each test opens its own small receiving socket on 127.0.0.1 (the
//   "loopback" address, meaning this same computer). Loopback traffic never
//   leaves the machine, so these tests work with no internet connection.
//
//   The receiver asks for port 0, which tells the operating system "give me
//   any free port". That avoids the classic test failure where a fixed port
//   number is already in use by something else.
// =============================================================================
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cstdint>

#include "udp_sender.hpp"

namespace {

using telemetry::UdpSender;

// A minimal UDP receiver used only by these tests.
class LoopbackReceiver {
public:
    LoopbackReceiver() {
        socket_descriptor_ = ::socket(AF_INET, SOCK_DGRAM, 0);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1
        address.sin_port = 0;                              // any free port
        ::bind(socket_descriptor_, reinterpret_cast<sockaddr*>(&address), sizeof(address));

        // Ask the operating system which port it actually chose.
        socklen_t address_size = sizeof(address);
        ::getsockname(socket_descriptor_, reinterpret_cast<sockaddr*>(&address), &address_size);
        port_ = ntohs(address.sin_port);

        // Give up waiting after 2 seconds so a failing test cannot hang forever.
        timeval timeout{};
        timeout.tv_sec = 2;
        ::setsockopt(socket_descriptor_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }

    ~LoopbackReceiver() { ::close(socket_descriptor_); }

    std::uint16_t port() const { return port_; }

    // Waits for one datagram. Returns its size in bytes, or -1 on timeout.
    long receive(std::uint8_t* buffer, std::size_t buffer_size) {
        return static_cast<long>(::recv(socket_descriptor_, buffer, buffer_size, 0));
    }

private:
    int socket_descriptor_ = -1;
    std::uint16_t port_ = 0;
};

TEST(UdpSender, DeliversBytesUnchanged) {
    LoopbackReceiver receiver;
    UdpSender sender;
    ASSERT_TRUE(sender.open("127.0.0.1", receiver.port())) << sender.last_error();

    const std::array<std::uint8_t, 5> message = {0xAA, 0x55, 0x01, 0x02, 0x03};
    ASSERT_TRUE(sender.send_bytes(message.data(), message.size())) << sender.last_error();

    std::array<std::uint8_t, 64> received{};
    const long received_size = receiver.receive(received.data(), received.size());

    ASSERT_EQ(received_size, 5);
    for (std::size_t i = 0; i < message.size(); ++i) {
        EXPECT_EQ(received[i], message[i]) << "byte " << i;
    }
}

// UDP keeps message boundaries: two sends arrive as two separate datagrams,
// never glued together. (TCP does not promise this.)
TEST(UdpSender, EachSendArrivesAsItsOwnDatagram) {
    LoopbackReceiver receiver;
    UdpSender sender;
    ASSERT_TRUE(sender.open("127.0.0.1", receiver.port()));

    const std::array<std::uint8_t, 3> first = {1, 2, 3};
    const std::array<std::uint8_t, 4> second = {4, 5, 6, 7};
    ASSERT_TRUE(sender.send_bytes(first.data(), first.size()));
    ASSERT_TRUE(sender.send_bytes(second.data(), second.size()));

    std::array<std::uint8_t, 64> received{};
    EXPECT_EQ(receiver.receive(received.data(), received.size()), 3);
    EXPECT_EQ(receiver.receive(received.data(), received.size()), 4);
}

TEST(UdpSender, RejectsInvalidAddress) {
    UdpSender sender;

    EXPECT_FALSE(sender.open("not-an-ip-address", 5005));
    EXPECT_FALSE(sender.is_open());
    EXPECT_FALSE(sender.last_error().empty());
}

TEST(UdpSender, HostNamesAreNotSupported) {
    UdpSender sender;

    EXPECT_FALSE(sender.open("localhost", 5005));
}

TEST(UdpSender, SendFailsBeforeOpen) {
    UdpSender sender;
    const std::uint8_t byte = 0x42;

    EXPECT_FALSE(sender.send_bytes(&byte, 1));
}

// Sending to a port with no listener still reports success. This documents a
// real property of UDP: the sender cannot tell whether anyone is listening.
// It is why the daemon keeps running happily with no viewer attached.
TEST(UdpSender, SendingToAPortWithNoListenerStillSucceeds) {
    std::uint16_t unused_port = 0;
    {
        LoopbackReceiver temporary;  // grab a free port number...
        unused_port = temporary.port();
    }                                // ...then close it again

    UdpSender sender;
    ASSERT_TRUE(sender.open("127.0.0.1", unused_port));
    const std::uint8_t byte = 0x42;

    EXPECT_TRUE(sender.send_bytes(&byte, 1));
    EXPECT_TRUE(sender.send_bytes(&byte, 1));
}

TEST(UdpSender, CloseIsSafeToCallTwice) {
    UdpSender sender;
    ASSERT_TRUE(sender.open("127.0.0.1", 5005));

    sender.close();
    sender.close();

    EXPECT_FALSE(sender.is_open());
}

}  // namespace
