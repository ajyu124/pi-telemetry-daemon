// =============================================================================
// udp_sender.hpp
//
// PURPOSE
//   Sends blocks of bytes to another program over the network using UDP.
//
// TERMS
//   Socket:  the operating system's handle for a network connection. On Linux
//            it is just an integer called a file descriptor.
//   UDP:     User Datagram Protocol. Each send is one independent packet
//            (a "datagram"). There is no connection, no retry and no delivery
//            guarantee. A packet either arrives whole or not at all.
//   TCP:     the alternative. It guarantees delivery and ordering by
//            re-sending lost data, which can delay everything behind it.
//   Port:    a number from 1 to 65535 that identifies which program on the
//            destination machine should receive the packet.
//
// WHY UDP FOR TELEMETRY?
//   Telemetry is a stream of "latest value" messages. If a frame is lost, the
//   next one arrives 20 ms later and is more useful than a late copy of the
//   old one. UDP also means the sender never blocks waiting for a slow or
//   absent receiver. The cost is that loss must be detected by the receiver,
//   which is what the sequence number in each frame is for.
//   See docs/DESIGN_DECISIONS.md, decision 5.
//
// RESOURCE SAFETY (RAII)
//   The destructor closes the socket, so the socket can never leak even if
//   the program exits early. Tying a resource's lifetime to an object like
//   this is a core C++ idiom called RAII ("resource acquisition is
//   initialization"). Copying is disabled, because two objects closing the
//   same socket would be a bug.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace telemetry {

class UdpSender {
public:
    UdpSender() = default;
    ~UdpSender();

    // Disable copying (see RAII note above).
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    // Creates the socket and remembers where to send.
    //   destination_ip:   an IPv4 address in dotted form, such as "127.0.0.1".
    //                     Host names like "localhost" are not supported.
    //   destination_port: 1 to 65535.
    // Returns true on success. On failure, last_error() explains why.
    bool open(const std::string& destination_ip, std::uint16_t destination_port);

    // Sends one datagram. Returns true if the operating system accepted all
    // the bytes. NOTE: true does not mean the receiver got it. UDP gives no
    // such confirmation, and sending to a port nobody is listening on still
    // returns true.
    bool send_bytes(const std::uint8_t* data, std::size_t byte_count);

    // Closes the socket. Safe to call more than once.
    void close();

    bool is_open() const { return socket_descriptor_ >= 0; }
    const std::string& last_error() const { return last_error_; }

private:
    static constexpr int kNoSocket = -1;

    int socket_descriptor_ = kNoSocket;
    std::uint32_t destination_address_network_order_ = 0;
    std::uint16_t destination_port_ = 0;
    std::string last_error_;
};

}  // namespace telemetry
