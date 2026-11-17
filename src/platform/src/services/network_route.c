/* SPDX-License-Identifier: Apache-2.0 */
#define _DEFAULT_SOURCE
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "network_route.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>

int rv1126b_network_route_ready(const char *interface_name, uint32_t excluded_ipv4) {
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses) < 0) {
        return -1;
    }
    FILE *routes = fopen("/proc/net/route", "r");
    if (!routes) {
        freeifaddrs(addresses);
        return -1;
    }
    int ready = 0;
    char line[512];
    while (fgets(line, sizeof(line), routes)) {
        char name[IFNAMSIZ];
        unsigned long destination, gateway, flags, mask;
        if (sscanf(line, "%15s %lx %lx %lx %*u %*u %*u %lx", name, &destination, &gateway, &flags,
                   &mask) != 5 ||
            destination != 0 || mask != 0 || !(flags & 1UL) || (flags & 0x200UL) ||
            (interface_name && interface_name[0] && strcmp(name, interface_name) != 0)) {
            continue;
        }
        for (const struct ifaddrs *it = addresses; it; it = it->ifa_next) {
            if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
                strcmp(it->ifa_name, name) != 0 || !(it->ifa_flags & IFF_UP) ||
                !(it->ifa_flags & IFF_RUNNING) || (it->ifa_flags & IFF_LOOPBACK)) {
                continue;
            }
            uint32_t ip = ntohl(((const struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr);
            if (ip != excluded_ipv4 && ip != 0 && (ip >> 24) != 127 && (ip >> 16) != 0xa9fe &&
                (ip >> 28) < 14) {
                ready = 1;
                break;
            }
        }
        if (ready) {
            break;
        }
    }
    if (ferror(routes)) {
        errno = EIO;
        ready = -1;
    }
    fclose(routes);
    freeifaddrs(addresses);
    return ready;
}
