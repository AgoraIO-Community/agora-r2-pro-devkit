/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "rv1126b_platform.h"

#include <api/aosl_log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    pthread_t thread;
    int wake[2];
    mybot_wifi_event_handler_t emit;
    void *user_data;
} network_context_t;

int rv1126b_network_ready(const char *interface_name) {
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses) < 0) {
        AOSL_LOG_ERR("getifaddrs: %s", strerror(errno));
        return -1;
    }
    FILE *routes = fopen("/proc/net/route", "r");
    if (!routes) {
        AOSL_LOG_ERR("cannot read IPv4 routes: %s", strerror(errno));
        freeifaddrs(addresses);
        return -1;
    }
    int ready = 0;
    char line[512];
    while (fgets(line, sizeof(line), routes)) {
        char name[IFNAMSIZ];
        unsigned long destination, gateway, flags, mask;
        if (sscanf(line, "%15s %lx %lx %lx %*u %*u %*u %lx",
                   name, &destination, &gateway, &flags, &mask) != 5 ||
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
            if (ip != 0 && (ip >> 24) != 127 && (ip >> 16) != 0xa9fe && (ip >> 28) < 14) {
                ready = 1;
                break;
            }
        }
        if (ready) {
            break;
        }
    }
    if (ferror(routes)) {
        AOSL_LOG_ERR("IPv4 route read failed: %s", strerror(errno));
        ready = -1;
    }
    fclose(routes);
    freeifaddrs(addresses);
    return ready;
}

static void *network_worker(void *opaque) {
    network_context_t *ctx = opaque;
    int previous = -2;
    struct pollfd wake = {.fd = ctx->wake[0], .events = POLLIN};
    for (;;) {
        int ret = poll(&wake, 1, 0);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        if (ret < 0 || wake.revents) {
            if (ret < 0) {
                AOSL_LOG_ERR("network monitor poll: %s", strerror(errno));
                ctx->emit(MYBOT_WIFI_EVENT_FAILED, ctx->user_data);
            }
            break;
        }
        int current = rv1126b_network_ready(rv1126b_platform_config()->network_interface);
        if (current != previous) {
            ctx->emit(current > 0 ? MYBOT_WIFI_EVENT_STA_CONNECTED :
                      current == 0 ? MYBOT_WIFI_EVENT_STA_DISCONNECTED : MYBOT_WIFI_EVENT_FAILED,
                      ctx->user_data);
            previous = current;
        }
        ret = poll(&wake, 1, 1000);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        if (ret < 0 || wake.revents) {
            if (ret < 0) {
                AOSL_LOG_ERR("network monitor poll: %s", strerror(errno));
                ctx->emit(MYBOT_WIFI_EVENT_FAILED, ctx->user_data);
            }
            break;
        }
    }
    return NULL;
}

static int network_init(void **out, const char *device_id,
                        mybot_wifi_event_handler_t emit, void *user_data) {
    (void)device_id;
    if (!out || !emit) {
        AOSL_LOG_ERR("invalid network listener");
        return -1;
    }
    network_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        AOSL_LOG_ERR("cannot allocate network listener");
        return -1;
    }
    if (pipe2(ctx->wake, O_CLOEXEC | O_NONBLOCK) < 0) {
        AOSL_LOG_ERR("network wake pipe: %s", strerror(errno));
        free(ctx);
        return -1;
    }
    ctx->emit = emit;
    ctx->user_data = user_data;
    int err = pthread_create(&ctx->thread, NULL, network_worker, ctx);
    if (err != 0) {
        AOSL_LOG_ERR("network thread: %s", strerror(err));
        close(ctx->wake[0]);
        close(ctx->wake[1]);
        free(ctx);
        return -1;
    }
    *out = ctx;
    return 0;
}

static void network_destroy(void *opaque) {
    network_context_t *ctx = opaque;
    if (!ctx) {
        return;
    }
    /* Closing the sole writer wakes the poller without a blocking pipe write. */
    close(ctx->wake[1]);
    int err = pthread_join(ctx->thread, NULL);
    if (err != 0) {
        AOSL_LOG_ERR("cannot join network monitor: %s", strerror(err));
        abort();
    }
    close(ctx->wake[0]);
    free(ctx);
}

const mybot_wifi_ops_t *rv1126b_network_ops(void) {
    static const mybot_wifi_ops_t ops = {.init = network_init, .destroy = network_destroy};
    return &ops;
}
