// SPDX-License-Identifier: MIT
#pragma once

#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <cstring>

namespace pace::services {

inline bool is_setup_address(const sockaddr_storage &address, socklen_t size) {
    if (address.ss_family == AF_INET && size >= sizeof(sockaddr_in)) {
        sockaddr_in ipv4{};
        std::memcpy(&ipv4, &address, sizeof(ipv4));
        return ipv4.sin_addr.s_addr == inet_addr("192.168.4.1");
    }
    if (address.ss_family == AF_INET6 && size >= sizeof(sockaddr_in6)) {
        sockaddr_in6 ipv6{};
        std::memcpy(&ipv6, &address, sizeof(ipv6));
        // ESP-IDF's dual-stack listener reports IPv4 clients as mapped IPv6.
        constexpr unsigned char expected[16] = {0, 0, 0,    0,    0,   0,   0, 0,
                                                0, 0, 0xff, 0xff, 192, 168, 4, 1};
        return std::memcmp(&ipv6.sin6_addr, expected, sizeof(expected)) == 0;
    }
    return false;
}

} // namespace pace::services
