/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "wifi_backend_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void wifi_control_init(wifi_control_t *control) {
    memset(control, 0, sizeof(*control));
    control->descriptor = -1;
}

void wifi_control_close(wifi_control_t *control) {
    if (control->descriptor >= 0)
        close(control->descriptor);
    control->descriptor = -1;
    if (control->local[0])
        unlink(control->local);
    control->local[0] = 0;
}

bool wifi_control_open(wifi_control_t *control, const char *remote, const char *local) {
    struct sockaddr_un address;
    wifi_control_close(control);
    if (strlen(remote) >= sizeof(address.sun_path) || strlen(local) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return false;
    }
    control->descriptor = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (control->descriptor < 0)
        return false;
    fcntl(control->descriptor, F_SETFD, FD_CLOEXEC);
    fcntl(control->descriptor, F_SETFL, O_NONBLOCK);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, local, strlen(local) + 1);
    if (bind(control->descriptor, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int code = errno;
        wifi_control_close(control);
        errno = code;
        return false;
    }
    wifi_copy(control->local, sizeof(control->local), local);
    memset(address.sun_path, 0, sizeof(address.sun_path));
    memcpy(address.sun_path, remote, strlen(remote) + 1);
    if (connect(control->descriptor, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int code = errno;
        wifi_control_close(control);
        errno = code;
        return false;
    }
    return true;
}

bool wifi_control_request(wifi_control_t *control, const char *command, unsigned timeout_ms,
                          wifi_buffer_t *response) {
    char bytes[65536];
    uint64_t deadline = wifi_now_ms() + timeout_ms;
    response->length = 0;
    if (response->data)
        response->data[0] = 0;
    if (control->descriptor < 0)
        return false;
    while (recv(control->descriptor, bytes, sizeof(bytes), MSG_DONTWAIT) > 0) {
    }
    if (send(control->descriptor, command, strlen(command), 0) != (ssize_t)strlen(command))
        return false;
    while (wifi_now_ms() < deadline) {
        struct pollfd ready = {control->descriptor, POLLIN, 0};
        uint64_t now = wifi_now_ms();
        int result = poll(&ready, 1, (int)(deadline > now ? deadline - now : 0));
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0 || !(ready.revents & POLLIN))
            return false;
        ssize_t used = recv(control->descriptor, bytes, sizeof(bytes), 0);
        if (used > 0 && bytes[0] != '<')
            return !memchr(bytes, 0, (size_t)used) &&
                   wifi_buffer_append(response, bytes, (size_t)used);
    }
    return false;
}

bool wifi_control_ok(wifi_control_t *control, const char *command) {
    wifi_buffer_t response;
    wifi_buffer_init(&response, 65536);
    bool ok =
        wifi_control_request(control, command, 200, &response) && !strncmp(response.data, "OK", 2);
    wifi_buffer_free(&response);
    return ok;
}

bool wifi_reconfigure_control(const char *const *remotes, const char *local, unsigned timeout_ms) {
    uint64_t deadline = wifi_now_ms() + timeout_ms;
    bool accepted = false;
    while (wifi_now_ms() < deadline) {
        wifi_control_t current;
        wifi_control_init(&current);
        wifi_buffer_t reply;
        wifi_buffer_init(&reply, 65536);
        bool attached = false;
        for (size_t i = 0; remotes && remotes[i]; ++i) {
            if (wifi_control_open(&current, remotes[i], local) &&
                wifi_control_request(&current, "PING", 100, &reply) &&
                !strncmp(reply.data, "PONG", 4)) {
                attached = true;
                break;
            }
        }
        bool rejected = false;
        if (attached && !accepted) {
            uint64_t now = wifi_now_ms();
            unsigned remaining = (unsigned)(deadline > now ? deadline - now : 0);
            if (remaining && wifi_control_request(&current, "RECONFIGURE",
                                                  remaining < 1000 ? remaining : 1000, &reply)) {
                if (!strncmp(reply.data, "OK", 2))
                    accepted = true;
                else if (reply.length)
                    rejected = true;
            }
            attached = false; /* A fresh PING must prove the post-reload endpoint. */
        }
        wifi_buffer_free(&reply);
        wifi_control_close(&current);
        if (rejected)
            return false;
        if (attached && accepted)
            return true;
        wifi_sleep_ms(25);
    }
    return false;
}
