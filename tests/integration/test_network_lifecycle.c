/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "platform_internal.h"
#include "check.h"

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
#include <time.h>
#include <unistd.h>

static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_changed = PTHREAD_COND_INITIALIZER;
static const char default_route[] = "eth0 00000000 0100000A 0003 0 0 0 00000000 0 0 0\n";
static struct {
    const char *routes;
    const char *ip;
    unsigned flags;
    int addresses_error, routes_error, read_error;
} fixture = {default_route, "10.0.0.2", IFF_UP | IFF_RUNNING, 0, 0, 0};
static int contexts, address_lists, route_files, wake_fds[2] = {-1, -1};
static int fail_pipe, fail_thread;
static unsigned inspections, callbacks;
static mybot_wifi_event_t events[16];
static bool block_callback, callback_entered, release_callback, destroy_entered, destroyed;
static const rv1126b_platform_config_t config = {.network_interface = "eth0"};

const rv1126b_platform_config_t *rv1126b_platform_config(void) {
    return &config;
}
static void *network_calloc(size_t count, size_t size) {
    void *result = calloc(count, size);
    if (result)
        ++contexts;
    return result;
}
static void network_free(void *pointer) {
    if (pointer)
        --contexts;
    free(pointer);
}
static int network_pipe2(int descriptors[2], int flags) {
    CHECK(flags == (O_CLOEXEC | O_NONBLOCK));
    if (fail_pipe) {
        errno = EMFILE;
        return -1;
    }
    CHECK(wake_fds[0] == -1 && wake_fds[1] == -1);
    if (pipe(descriptors))
        return -1;
    for (int i = 0; i < 2; ++i) {
        CHECK(fcntl(descriptors[i], F_SETFD, FD_CLOEXEC) == 0);
        CHECK(fcntl(descriptors[i], F_SETFL, O_NONBLOCK) == 0);
        wake_fds[i] = descriptors[i];
    }
    return 0;
}
static int network_close(int descriptor) {
    if (descriptor == wake_fds[0])
        wake_fds[0] = -1;
    else if (descriptor == wake_fds[1])
        wake_fds[1] = -1;
    else
        CHECK(false);
    return close(descriptor);
}
static int network_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                                  void *(*worker)(void *), void *argument) {
    return fail_thread ? EAGAIN : pthread_create(thread, attributes, worker, argument);
}
static int fixture_getifaddrs(struct ifaddrs **addresses);
static void fixture_freeifaddrs(struct ifaddrs *addresses);
static FILE *fixture_fopen(const char *path, const char *mode);
static int fixture_fclose(FILE *file);
static int fixture_ferror(FILE *file);

#define calloc network_calloc
#define free network_free
#define pipe2 network_pipe2
#define close network_close
#define pthread_create network_pthread_create
#define getifaddrs fixture_getifaddrs
#define freeifaddrs fixture_freeifaddrs
#define fopen fixture_fopen
#define fclose fixture_fclose
#define ferror fixture_ferror
#include "../../src/platform/src/services/network_monitor.c"
#undef calloc
#undef free
#undef pipe2
#undef close
#undef pthread_create
#undef getifaddrs
#undef freeifaddrs
#undef fopen
#undef fclose
#undef ferror

typedef struct {
    struct ifaddrs address;
    struct sockaddr_in ip;
    char name[8];
} fixture_addresses_t;
static int fixture_getifaddrs(struct ifaddrs **addresses) {
    pthread_mutex_lock(&fixture_lock);
    ++inspections;
    pthread_cond_broadcast(&fixture_changed);
    if (fixture.addresses_error) {
        pthread_mutex_unlock(&fixture_lock);
        errno = EIO;
        return -1;
    }
    fixture_addresses_t *result = calloc(1, sizeof(*result));
    CHECK(result);
    result->ip.sin_family = AF_INET;
    CHECK(inet_pton(AF_INET, fixture.ip, &result->ip.sin_addr) == 1);
    strcpy(result->name, "eth0");
    result->address.ifa_name = result->name;
    result->address.ifa_addr = (struct sockaddr *)&result->ip;
    result->address.ifa_flags = fixture.flags;
    *addresses = &result->address;
    ++address_lists;
    pthread_mutex_unlock(&fixture_lock);
    return 0;
}
static void fixture_freeifaddrs(struct ifaddrs *addresses) {
    pthread_mutex_lock(&fixture_lock);
    CHECK(address_lists > 0);
    --address_lists;
    pthread_mutex_unlock(&fixture_lock);
    free(addresses);
}
static FILE *fixture_fopen(const char *path, const char *mode) {
    CHECK(strcmp(path, "/proc/net/route") == 0 && strcmp(mode, "r") == 0);
    pthread_mutex_lock(&fixture_lock);
    FILE *file = NULL;
    if (!fixture.routes_error) {
        file = tmpfile();
        CHECK(file && fputs(fixture.routes, file) >= 0 && fseek(file, 0, SEEK_SET) == 0);
        ++route_files;
    } else
        errno = EACCES;
    pthread_mutex_unlock(&fixture_lock);
    return file;
}
static int fixture_fclose(FILE *file) {
    pthread_mutex_lock(&fixture_lock);
    CHECK(route_files > 0);
    --route_files;
    pthread_mutex_unlock(&fixture_lock);
    return fclose(file);
}
static int fixture_ferror(FILE *file) {
    pthread_mutex_lock(&fixture_lock);
    int result = fixture.read_error || ferror(file);
    pthread_mutex_unlock(&fixture_lock);
    return result;
}
static void check_released(void) {
    CHECK(contexts == 0 && address_lists == 0 && route_files == 0);
    CHECK(wake_fds[0] == -1 && wake_fds[1] == -1);
}
static void check_readiness(int expected) {
    CHECK(rv1126b_network_ready("eth0") == expected);
    check_released();
}
static void test_ready_requires_usable_address_and_default_route(void) {
    check_readiness(1);
    CHECK(rv1126b_network_ready(NULL) == 1);
    CHECK(rv1126b_network_ready("wlan0") == 0);
    fixture.routes = "eth0 00000000 00000000 0201 0 0 0 00000000 0 0 0\n"; /* REJECT */
    check_readiness(0);
    fixture.routes = "eth0 0000000A 00000000 0001 0 0 0 00FFFFFF 0 0 0\n";
    check_readiness(0);
    fixture.routes = "eth0 00000000 00000000 0000 0 0 0 00000000 0 0 0\n"; /* Not UP */
    check_readiness(0);
    fixture.routes = "malformed route\n";
    check_readiness(0);
    fixture.routes = default_route;
    const char *invalid[] = {"0.0.0.0", "127.0.0.1", "169.254.2.3", "224.0.0.1", "255.255.255.255"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        fixture.ip = invalid[i];
        check_readiness(0);
    }
    fixture.ip = "10.0.0.2";
    fixture.flags = IFF_UP;
    check_readiness(0);
    fixture.flags = IFF_UP | IFF_RUNNING | IFF_LOOPBACK;
    check_readiness(0);
    fixture.flags = IFF_UP | IFF_RUNNING;
    fixture.addresses_error = 1;
    check_readiness(-1);
    fixture.addresses_error = 0;
    fixture.routes_error = 1;
    check_readiness(-1);
    fixture.routes_error = 0;
    fixture.read_error = 1;
    check_readiness(-1);
    fixture.read_error = 0;
}
static void handler(mybot_wifi_event_t event, void *user_data) {
    CHECK(user_data == &callbacks);
    pthread_mutex_lock(&fixture_lock);
    CHECK(callbacks < sizeof(events) / sizeof(events[0]));
    events[callbacks++] = event;
    callback_entered = true;
    pthread_cond_broadcast(&fixture_changed);
    while (block_callback && !release_callback)
        pthread_cond_wait(&fixture_changed, &fixture_lock);
    pthread_mutex_unlock(&fixture_lock);
}
static struct timespec deadline(void) {
    struct timespec result;
    CHECK(clock_gettime(CLOCK_REALTIME, &result) == 0);
    result.tv_sec += 5;
    return result;
}
static uint64_t monotonic_ms(void) {
    struct timespec result;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &result) == 0);
    return (uint64_t)result.tv_sec * 1000 + (uint64_t)result.tv_nsec / 1000000;
}
static void wait_counts(unsigned minimum_callbacks, unsigned minimum_inspections) {
    struct timespec until = deadline();
    pthread_mutex_lock(&fixture_lock);
    while (callbacks < minimum_callbacks || inspections < minimum_inspections)
        CHECK(pthread_cond_timedwait(&fixture_changed, &fixture_lock, &until) == 0);
    pthread_mutex_unlock(&fixture_lock);
}
static void *destroy_worker(void *context) {
    pthread_mutex_lock(&fixture_lock);
    destroy_entered = true;
    pthread_cond_broadcast(&fixture_changed);
    pthread_mutex_unlock(&fixture_lock);
    rv1126b_network_ops()->destroy(context);
    pthread_mutex_lock(&fixture_lock);
    destroyed = true;
    pthread_cond_broadcast(&fixture_changed);
    pthread_mutex_unlock(&fixture_lock);
    return NULL;
}
static void test_init_failure_and_worker_shutdown(void) {
    const mybot_wifi_ops_t *ops = rv1126b_network_ops();
    void *context = NULL;
    CHECK(ops->init(&context, "unused", NULL, &callbacks) == -1);
    fail_pipe = 1;
    CHECK(ops->init(&context, "unused", handler, &callbacks) == -1 && !context);
    check_released();
    fail_pipe = 0;
    fail_thread = 1;
    CHECK(ops->init(&context, "unused", handler, &callbacks) == -1 && !context);
    check_released();
    fail_thread = 0;
    inspections = callbacks = 0;
    CHECK(ops->init(&context, "unused", handler, &callbacks) == 0);
    wait_counts(1, 3); /* Poll identical snapshots twice without duplicate state callbacks. */
    pthread_mutex_lock(&fixture_lock);
    CHECK(callbacks == 1 && events[0] == MYBOT_WIFI_EVENT_STA_CONNECTED);
    fixture.routes = "";
    pthread_mutex_unlock(&fixture_lock);
    wait_counts(2, 0);
    pthread_mutex_lock(&fixture_lock);
    CHECK(events[1] == MYBOT_WIFI_EVENT_STA_DISCONNECTED);
    fixture.addresses_error = 1;
    pthread_mutex_unlock(&fixture_lock);
    wait_counts(3, 0);
    pthread_mutex_lock(&fixture_lock);
    CHECK(events[2] == MYBOT_WIFI_EVENT_FAILED);
    pthread_mutex_unlock(&fixture_lock);
    uint64_t started = monotonic_ms();
    ops->destroy(context);
    CHECK(monotonic_ms() - started < 500); /* Wake-up interrupts the one-second poll. */
    check_released();

    fixture.addresses_error = 0;
    fixture.routes = default_route;
    block_callback = true;
    callback_entered = false;
    unsigned before = callbacks;
    CHECK(ops->init(&context, "unused", handler, &callbacks) == 0);
    wait_counts(before + 1, 0);
    pthread_t stopping;
    CHECK(pthread_create(&stopping, NULL, destroy_worker, context) == 0);
    struct timespec until = deadline();
    pthread_mutex_lock(&fixture_lock);
    while (!destroy_entered)
        CHECK(pthread_cond_timedwait(&fixture_changed, &fixture_lock, &until) == 0);
    CHECK(clock_gettime(CLOCK_REALTIME, &until) == 0);
    until.tv_nsec += 30000000;
    if (until.tv_nsec >= 1000000000) {
        until.tv_nsec -= 1000000000;
        ++until.tv_sec;
    }
    while (!destroyed) {
        int result = pthread_cond_timedwait(&fixture_changed, &fixture_lock, &until);
        if (result == ETIMEDOUT)
            break;
        CHECK(result == 0);
    }
    CHECK(callback_entered && !destroyed);
    release_callback = true;
    pthread_cond_broadcast(&fixture_changed);
    pthread_mutex_unlock(&fixture_lock);
    CHECK(pthread_join(stopping, NULL) == 0);
    CHECK(destroyed && callbacks == before + 1);
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 30000000};
    nanosleep(&pause, NULL);
    CHECK(callbacks == before + 1); /* A destroyed listener can no longer call its owner. */
    check_released();
    for (int i = 0; i < 5; ++i) {
        CHECK(ops->init(&context, "unused", handler, &callbacks) == 0);
        ops->destroy(context);
        check_released();
    }
}
int main(void) {
    test_ready_requires_usable_address_and_default_route();
    test_init_failure_and_worker_shutdown();
    puts("Network route/address readiness, failure cleanup, transitions and callback join: PASS");
    return 0;
}
