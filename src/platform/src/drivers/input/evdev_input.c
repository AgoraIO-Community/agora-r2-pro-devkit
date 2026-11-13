/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "platform_internal.h"
#include "input_logic.h"

#include <api/aosl_log.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int fd;
    unsigned char partial[sizeof(struct input_event)];
    size_t used;
} input_source_t;
typedef struct {
    pthread_t thread;
    int wake[2];
    int stdin_fd;
    input_source_t key, touch;
    rv1126b_touch_t touch_state;
    bool stopping;
    mybot_key_event_handler_t emit;
    void *user;
} input_context_t;
static int s_exit_requested;

static uint32_t now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static int open_input(const char *path, const char *name) {
    if (strcmp(path, "none") == 0 || !path[0] || strcmp(path, "stdin") == 0)
        return -1;
    if (strcmp(path, "auto") != 0) {
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        int version;
        if (fd >= 0 && ioctl(fd, EVIOCGVERSION, &version) < 0) {
            close(fd);
            fd = -1;
        }
        return fd;
    }
    glob_t nodes = {0};
    int found = -1;
    if (glob("/dev/input/event*", 0, NULL, &nodes) == 0) {
        for (size_t i = 0; i < nodes.gl_pathc; ++i) {
            int fd = open(nodes.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0)
                continue;
            char actual[128] = {0};
            if (ioctl(fd, EVIOCGNAME(sizeof(actual) - 1), actual) >= 0 &&
                strcmp(actual, name) == 0) {
                found = fd;
                AOSL_LOG_NTC("input: %s at %s", name, nodes.gl_pathv[i]);
                break;
            }
            close(fd);
        }
    }
    globfree(&nodes);
    return found;
}

static void emit(input_context_t *ctx, mybot_key_event_t event) {
    static const char *const names[] = {"conversation_start", "conversation_stop", "pair", "exit",
                                        "volume_up",          "volume_down"};
    AOSL_LOG_NTC("input: %s", names[event]);
    if (event == MYBOT_KEY_EVENT_EXIT)
        __atomic_store_n(&s_exit_requested, 1, __ATOMIC_RELEASE);
    ctx->emit(event, ctx->user);
}

static void stdin_key(input_context_t *ctx, char code) {
    mybot_state_t state = mybot_get_state();
    switch (code) {
    case 's':
        if (state == MYBOT_STATE_READY)
            emit(ctx, MYBOT_KEY_EVENT_CONVERSATION_START);
        break;
    case 'q':
        if (state == MYBOT_STATE_IN_CONVERSATION)
            emit(ctx, MYBOT_KEY_EVENT_CONVERSATION_STOP);
        break;
    case 'p':
        emit(ctx, MYBOT_KEY_EVENT_PAIR);
        break;
    case 'u':
        emit(ctx, MYBOT_KEY_EVENT_VOLUME_UP);
        break;
    case 'd':
        emit(ctx, MYBOT_KEY_EVENT_VOLUME_DOWN);
        break;
    case 'e':
        emit(ctx, MYBOT_KEY_EVENT_EXIT);
        break;
    default:
        break;
    }
}

static int drain_input(input_context_t *ctx, input_source_t *source, bool touch) {
    for (unsigned count = 0; count < 64 && !__atomic_load_n(&ctx->stopping, __ATOMIC_ACQUIRE);
         ++count) {
        ssize_t n = read(source->fd, source->partial + source->used,
                         sizeof(source->partial) - source->used);
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return 0;
        if (n <= 0)
            return -1;
        source->used += (size_t)n;
        if (source->used != sizeof(struct input_event))
            continue;
        struct input_event input;
        memcpy(&input, source->partial, sizeof(input));
        source->used = 0;
        mybot_key_event_t event;
        int ready = touch
                        ? rv1126b_touch_event(&ctx->touch_state, input.type, input.code,
                                              input.value, now_ms(), mybot_get_state(), &event)
                        : input.type == EV_KEY && input.value == 1 &&
                              rv1126b_key_translate(input.code, rv1126b_platform_config()->talk_key,
                                                    mybot_get_state(), &event);
        if (ready)
            emit(ctx, event);
    }
    return 0;
}

static void *input_worker(void *opaque) {
    input_context_t *ctx = opaque;
    while (!__atomic_load_n(&ctx->stopping, __ATOMIC_ACQUIRE)) {
        struct pollfd fds[4] = {{ctx->wake[0], POLLIN, 0},
                                {ctx->stdin_fd, POLLIN, 0},
                                {ctx->key.fd, POLLIN, 0},
                                {ctx->touch.fd, POLLIN, 0}};
        int ret = poll(fds, 4, -1);
        if (ret < 0 && errno == EINTR)
            continue;
        if (ret < 0 || fds[0].revents || __atomic_load_n(&ctx->stopping, __ATOMIC_ACQUIRE))
            break;
        if (fds[1].revents & POLLIN) {
            char data[64];
            ssize_t n = read(ctx->stdin_fd, data, sizeof(data));
            if (n <= 0) {
                close(ctx->stdin_fd);
                ctx->stdin_fd = -1;
            } else
                for (ssize_t i = 0; i < n; ++i)
                    stdin_key(ctx, data[i]);
        }
        if (ctx->stdin_fd >= 0 && (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL))) {
            close(ctx->stdin_fd);
            ctx->stdin_fd = -1;
        }
        input_source_t *sources[2] = {&ctx->key, &ctx->touch};
        for (int i = 0; i < 2; ++i) {
            short events = fds[i + 2].revents;
            if (sources[i]->fd >= 0 &&
                ((events & (POLLHUP | POLLERR | POLLNVAL)) ||
                 ((events & POLLIN) && drain_input(ctx, sources[i], i == 1) < 0))) {
                AOSL_LOG_WRN("input: %s source disconnected", i ? "touch" : "key");
                close(sources[i]->fd);
                sources[i]->fd = -1;
            }
        }
    }
    return NULL;
}

static void close_context(input_context_t *ctx) {
    if (ctx->stdin_fd >= 0)
        close(ctx->stdin_fd);
    if (ctx->key.fd >= 0)
        close(ctx->key.fd);
    if (ctx->touch.fd >= 0)
        close(ctx->touch.fd);
    if (ctx->wake[0] >= 0)
        close(ctx->wake[0]);
    if (ctx->wake[1] >= 0)
        close(ctx->wake[1]);
    free(ctx);
}

static int key_init(void **out, mybot_key_event_handler_t handler, void *user) {
    if (!out || !handler)
        return -1;
    input_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return -1;
    ctx->stdin_fd = ctx->key.fd = ctx->touch.fd = ctx->wake[0] = ctx->wake[1] = -1;
    ctx->emit = handler;
    ctx->user = user;
    const rv1126b_platform_config_t *config = rv1126b_platform_config();
    ctx->key.fd = open_input(config->input_device, "adc-keys");
    ctx->touch.fd = open_input(config->touch_device, "axs_ts");
    const char *paths[2] = {config->input_device, config->touch_device};
    const int descriptors[2] = {ctx->key.fd, ctx->touch.fd};
    for (int i = 0; i < 2; ++i) {
        if (paths[i][0] && strcmp(paths[i], "auto") && strcmp(paths[i], "none") &&
            strcmp(paths[i], "stdin") && descriptors[i] < 0) {
            AOSL_LOG_ERR("input: cannot open requested device %s", paths[i]);
            close_context(ctx);
            return -1;
        }
    }
    bool explicit_stdin = !config->input_device[0] || strcmp(config->input_device, "stdin") == 0;
    if (explicit_stdin || (isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) == getpgrp()))
        ctx->stdin_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3);
    if (pipe2(ctx->wake, O_CLOEXEC | O_NONBLOCK) < 0) {
        close_context(ctx);
        return -1;
    }
    __atomic_store_n(&s_exit_requested, 0, __ATOMIC_RELEASE);
    if (pthread_create(&ctx->thread, NULL, input_worker, ctx) != 0) {
        close_context(ctx);
        return -1;
    }
    *out = ctx;
    AOSL_LOG_NTC("input: short touch/menu toggles conversation; left/right changes volume; "
                 "stdin s/q/p/u/d/e (Enter)");
    return 0;
}

static void key_destroy(void *opaque) {
    input_context_t *ctx = opaque;
    if (!ctx)
        return;
    __atomic_store_n(&ctx->stopping, true, __ATOMIC_RELEASE);
    if (ctx->wake[1] >= 0) {
        close(ctx->wake[1]);
        ctx->wake[1] = -1;
    }
    if (pthread_join(ctx->thread, NULL) != 0) {
        AOSL_LOG_ERR("input: join failed; retaining context");
        return;
    }
    close_context(ctx);
}

int rv1126b_key_exit_requested(void) {
    return __atomic_load_n(&s_exit_requested, __ATOMIC_ACQUIRE);
}

const mybot_key_ops_t *rv1126b_key_ops(void) {
    static const mybot_key_ops_t ops = {.init = key_init, .destroy = key_destroy};
    return &ops;
}
