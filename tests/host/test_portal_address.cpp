// SPDX-License-Identifier: MIT
#include "portal_address.hpp"
#include <cstdio>
#include <cstdlib>

namespace {

void check(int family, const char *ip, bool expected) {
    sockaddr_storage storage{};
    socklen_t size;
    if (family == AF_INET) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        if (inet_pton(AF_INET, ip, &address.sin_addr) != 1)
            std::abort();
        size = sizeof(address);
        std::memcpy(&storage, &address, size);
    } else {
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, ip, &address.sin6_addr) != 1)
            std::abort();
        size = sizeof(address);
        std::memcpy(&storage, &address, size);
    }
    if (pace::services::is_setup_address(storage, size) != expected ||
        pace::services::is_setup_address(storage, size - 1)) {
        std::fprintf(stderr, "Incorrect setup address decision: %s\n", ip);
        std::exit(1);
    }
}

} // namespace

int main() {
    check(AF_INET, "192.168.4.1", true);
    check(AF_INET6, "::ffff:192.168.4.1", true);
    check(AF_INET, "192.168.1.10", false);
    check(AF_INET6, "::ffff:192.168.1.10", false);
    check(AF_INET, "192.168.4.2", false);
    check(AF_INET6, "::ffff:192.168.4.2", false);
    check(AF_INET, "0.0.0.0", false);
    check(AF_INET6, "::", false);
    check(AF_INET6, "::1", false);
    check(AF_INET6, "::192.168.4.1", false);
    check(AF_INET6, "2001:db8::c0a8:401", false);
    sockaddr_storage unknown{};
    if (pace::services::is_setup_address(unknown, sizeof(unknown)))
        return 1;
    std::puts("Setup address checks passed");
}
