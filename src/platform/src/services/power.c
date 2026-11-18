/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/power.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define CONTROL_ACK_TIMEOUT_MS 1500

static int64_t power_now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int power_wait(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t now = power_now_ms();
        if (now < 0)
            return -1;
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        struct pollfd descriptor = {.fd = fd, .events = events};
        int ready = poll(&descriptor, 1, (int)(deadline - now));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            return -1;
        if (!ready) {
            errno = ETIMEDOUT;
            return -1;
        }
        return 0;
    }
}

static int request_transition(const char *request) {
    const char *runtime = getenv("MYBOT_SUPERVISOR_RUNTIME");
    if (!runtime || runtime[0] != '/') {
        errno = ENOTCONN;
        return -1;
    }
    struct stat status;
    if (lstat(runtime, &status) < 0)
        return -1;
    if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
        (status.st_mode & 07777) != 0700) {
        errno = EACCES;
        return -1;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int length = snprintf(address.sun_path, sizeof(address.sun_path), "%s/control.sock", runtime);
    if (length < 0 || (size_t)length >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (lstat(address.sun_path, &status) < 0)
        return -1;
    if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid() ||
        (status.st_mode & 07777) != 0600) {
        errno = EACCES;
        return -1;
    }
    int64_t now = power_now_ms();
    if (now < 0)
        return -1;
    int64_t deadline = now + CONTROL_ACK_TIMEOUT_MS;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int result = -1;
    int saved_errno;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        goto done;
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)) < 0)
        goto done;
#endif
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        if (errno != EINPROGRESS && errno != EAGAIN)
            goto done;
        if (power_wait(fd, POLLOUT, deadline) < 0)
            goto done;
        int error = 0;
        socklen_t error_size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0)
            goto done;
        if (error) {
            errno = error;
            goto done;
        }
    }
    const size_t length_to_send = strlen(request);
    size_t used = 0;
    while (used < length_to_send) {
        if (power_wait(fd, POLLOUT, deadline) < 0)
            goto done;
#ifdef MSG_NOSIGNAL
        ssize_t written = send(fd, request + used, length_to_send - used, MSG_NOSIGNAL);
#else
        ssize_t written = send(fd, request + used, length_to_send - used, 0);
#endif
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (written <= 0)
            goto done;
        used += (size_t)written;
    }
    char response[24] = {0};
    used = 0;
    while (used + 1 < sizeof(response)) {
        if (power_wait(fd, POLLIN, deadline) < 0)
            goto done;
        ssize_t count = read(fd, response + used, sizeof(response) - 1 - used);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (count <= 0) {
            errno = EPROTO;
            goto done;
        }
        used += (size_t)count;
        if (memchr(response, '\n', used))
            break;
    }
    if (used == 9 && !memcmp(response, "accepted\n", 9)) {
        /* A lost acknowledgement leaves the application running. The
         * supervisor acts only after this explicit commit. */
        const char commit[] = "commit\n";
        used = 0;
        while (used < sizeof(commit) - 1) {
            if (power_wait(fd, POLLOUT, deadline) < 0)
                goto done;
#ifdef MSG_NOSIGNAL
            ssize_t count = send(fd, commit + used, sizeof(commit) - 1 - used, MSG_NOSIGNAL);
#else
            ssize_t count = send(fd, commit + used, sizeof(commit) - 1 - used, 0);
#endif
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            if (count <= 0)
                goto done;
            used += (size_t)count;
        }
        memset(response, 0, sizeof(response));
        used = 0;
        while (used + 1 < sizeof(response)) {
            if (power_wait(fd, POLLIN, deadline) < 0)
                goto done;
            ssize_t count = read(fd, response + used, sizeof(response) - 1 - used);
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            if (count <= 0) {
                errno = EPROTO;
                goto done;
            }
            used += (size_t)count;
            if (memchr(response, '\n', used))
                break;
        }
        if (used == 10 && !memcmp(response, "committed\n", 10))
            result = 0;
        else
            errno = EACCES;
    } else
        errno = EACCES;
done:
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return result;
}

int rv1126b_poweroff_request(void) {
    return request_transition("poweroff\n");
}

int rv1126b_factory_reset_request(void) {
    return request_transition("factory_reset\n");
}
