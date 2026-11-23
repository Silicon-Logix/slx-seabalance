// Silicon LogiX / SLX SeaBalance
// Copyright (c) 2026 Marco Pezzullo (Silicon LogiX).
// Author: Marco Pezzullo
// License: Silicon LogiX Evaluation License 1.0. See LICENSE.

#include "wire.hpp"

#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace slx::sb::wire {
namespace {
#ifdef _WIN32
using Native = SOCKET;
using AddressLength = int;
constexpr Native invalid = INVALID_SOCKET;
struct Winsock {
    Winsock()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("Winsock initialization failed");
    }
    ~Winsock() { WSACleanup(); }
};
void init() { static Winsock winsock; }
int socket_error() { return WSAGetLastError(); }
#else
using Native = int;
using AddressLength = socklen_t;
constexpr Native invalid = -1;
void init() {}
int socket_error() { return errno; }
#endif

sockaddr_in address(Endpoint e)
{
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(e.port);
    a.sin_addr.s_addr = e.ip;
    return a;
}

std::runtime_error failed(const char *operation)
{
    const auto error = socket_error();
    return std::runtime_error(std::string(operation) + " failed (socket " + std::to_string(error) + ")");
}
} // namespace

Endpoint endpoint(const std::string &ipv4, uint16_t port_number)
{
    init();
    in_addr ip{};
    if (port_number == 0 || inet_pton(AF_INET, ipv4.c_str(), &ip) != 1)
        throw std::invalid_argument("Use a numeric IPv4 address and a port from 1 to 65535");
    return {ip.s_addr, port_number};
}

Socket::Socket()
{
    init();
    const auto fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == invalid)
        throw failed("UDP socket");
    handle_ = static_cast<uintptr_t>(fd);
}

Socket::~Socket()
{
#ifdef _WIN32
    closesocket(static_cast<Native>(handle_));
#else
    close(static_cast<Native>(handle_));
#endif
}

void Socket::bind(Endpoint e)
{
    const auto a = address(e);
    if (::bind(static_cast<Native>(handle_), reinterpret_cast<const sockaddr *>(&a), sizeof(a)) != 0)
        throw failed("UDP bind");
}

void Socket::send(const Packet &p, Endpoint e) const
{
    const auto a = address(e);
    const auto data = encode(p);
    const auto size = static_cast<int>(data.size);
    const auto sent = sendto(static_cast<Native>(handle_), reinterpret_cast<const char *>(data.bytes.data()),
                             size, 0, reinterpret_cast<const sockaddr *>(&a), sizeof(a));
    if (sent != size)
        throw failed("UDP send");
}

bool Socket::receive(Frame &frame, Endpoint &sender, std::chrono::milliseconds wait) const
{
    const auto fd = static_cast<Native>(handle_);
#ifdef _WIN32
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval timeout{static_cast<long>(wait.count() / 1000), static_cast<long>(wait.count() % 1000 * 1000)};
    const int ready = select(0, &fds, nullptr, nullptr, &timeout);
#else
    pollfd pfd{fd, POLLIN, 0};
    const int ready = poll(&pfd, 1, static_cast<int>(wait.count()));
    if (ready < 0 && errno == EINTR)
        return false;
#endif
    if (ready < 0)
        throw failed("UDP wait");
    if (ready == 0)
        return false;
    sockaddr_in a{};
    AddressLength len = sizeof(a);
    const auto count = recvfrom(fd, reinterpret_cast<char *>(frame.bytes.data()),
                                static_cast<int>(frame.bytes.size()), 0,
                                reinterpret_cast<sockaddr *>(&a), &len);
    sender = {a.sin_addr.s_addr, ntohs(a.sin_port)};
#ifdef _WIN32
    if (count < 0 && socket_error() == WSAEMSGSIZE) {
        frame.size = 0;
        return true;
    }
    if (count < 0 && socket_error() == WSAECONNRESET)
        return false;
#else
    if (count < 0 && errno == EINTR)
        return false;
#endif
    if (count < 0)
        throw failed("UDP receive");
    frame.size = static_cast<size_t>(count);
    return true;
}
} // namespace slx::sb::wire
