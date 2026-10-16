// =============================================================================
// udp_sender.cpp
//
// Implementation of UdpSender using the POSIX socket API, which is the
// standard networking interface on Linux and macOS. See udp_sender.hpp for
// what UDP is and why it was chosen.
// =============================================================================
#include "udp_sender.hpp"

#include <arpa/inet.h>   // inet_pton, htons
#include <netinet/in.h>  // sockaddr_in
#include <sys/socket.h>  // socket, sendto
#include <unistd.h>      // close

#include <cerrno>
#include <cstring>

namespace telemetry {

UdpSender::~UdpSender() {
    close();
}

bool UdpSender::open(const std::string& destination_ip, std::uint16_t destination_port) {
    close();  // allow open() to be called again on the same object

    // Convert the text address "127.0.0.1" into the 4-byte binary form the
    // operating system uses. inet_pton returns 1 on success.
    in_addr parsed_address{};
    if (inet_pton(AF_INET, destination_ip.c_str(), &parsed_address) != 1) {
        last_error_ = "'" + destination_ip + "' is not a valid IPv4 address (example: 127.0.0.1)";
        return false;
    }

    // AF_INET = IPv4, SOCK_DGRAM = datagram socket, which means UDP.
    const int descriptor = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (descriptor < 0) {
        last_error_ = std::string("could not create socket: ") + std::strerror(errno);
        return false;
    }

    socket_descriptor_ = descriptor;
    destination_address_network_order_ = parsed_address.s_addr;
    destination_port_ = destination_port;
    last_error_.clear();
    return true;
}

bool UdpSender::send_bytes(const std::uint8_t* data, std::size_t byte_count) {
    if (!is_open()) {
        last_error_ = "send_bytes called before open";
        return false;
    }

    // sockaddr_in is the structure that holds an IPv4 address and port.
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = destination_address_network_order_;
    // htons = "host to network short". Network protocols define port numbers
    // as big-endian, so this converts from the CPU's byte order.
    destination.sin_port = htons(destination_port_);

    const ssize_t bytes_sent =
        ::sendto(socket_descriptor_, data, byte_count, 0,
                 reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));

    if (bytes_sent < 0) {
        last_error_ = std::string("send failed: ") + std::strerror(errno);
        return false;
    }
    if (static_cast<std::size_t>(bytes_sent) != byte_count) {
        last_error_ = "send was truncated";
        return false;
    }
    return true;
}

void UdpSender::close() {
    if (socket_descriptor_ != kNoSocket) {
        ::close(socket_descriptor_);
        socket_descriptor_ = kNoSocket;
    }
}

}  // namespace telemetry
