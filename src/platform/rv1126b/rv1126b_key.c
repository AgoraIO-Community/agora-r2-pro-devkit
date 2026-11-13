/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "rv1126b_platform.h"

#include <api/aosl_log.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct {
    pthread_t thread;
    int fd;
    int wake[2];
    bool hardware;
    bool disabled;
    mybot_key_event_handler_t emit;
    void *user_data;
} key_context_t;

static pthread_mutex_t s_exit_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_exit_requested;
static bool s_auto_start_sent;

int rv1126b_key_exit_requested(void) {
    pthread_mutex_lock(&s_exit_lock);
    int requested = s_exit_requested;
    pthread_mutex_unlock(&s_exit_lock);
    return requested;
}

static void stdin_event(key_context_t *ctx, char key) {
    mybot_key_event_t event;
    switch (key) {
    case 's':
        if (mybot_get_state() != MYBOT_STATE_READY) {
            return;
        }
        event = MYBOT_KEY_EVENT_CONVERSATION_START;
        break;
    case 'q':
        if (mybot_get_state() != MYBOT_STATE_IN_CONVERSATION) {
            return;
        }
        event = MYBOT_KEY_EVENT_CONVERSATION_STOP;
        break;
    case 'p':
        event = MYBOT_KEY_EVENT_PAIR;
        break;
    case 'u':
        event = MYBOT_KEY_EVENT_VOLUME_UP;
        break;
    case 'd':
        event = MYBOT_KEY_EVENT_VOLUME_DOWN;
        break;
    case 'e':
        pthread_mutex_lock(&s_exit_lock);
        s_exit_requested = true;
        pthread_mutex_unlock(&s_exit_lock);
        event = MYBOT_KEY_EVENT_EXIT;
        break;
    default:
        return;
    }
    ctx->emit(event, ctx->user_data);
}

static void *key_worker(void *opaque) {
    key_context_t *ctx = opaque;
    struct pollfd fds[2] = {
        {.fd = ctx->wake[0], .events = POLLIN},
        {.fd = ctx->fd, .events = POLLIN},
    };
    for (;;) {
        int ret = poll(fds, 2, s_auto_start_sent ? -1 : 100);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        if (ret < 0 || fds[0].revents) {
            if (ret < 0) {
                AOSL_LOG_ERR("key poll: %s", strerror(errno));
                ctx->emit(MYBOT_KEY_EVENT_EXIT, ctx->user_data);
            }
            break;
        }
        if (!s_auto_start_sent && mybot_get_state() == MYBOT_STATE_READY) {
            s_auto_start_sent = true;
            AOSL_LOG_NTC("first READY: automatically requesting one conversation");
            ctx->emit(MYBOT_KEY_EVENT_CONVERSATION_START, ctx->user_data);
        }
        if (fds[1].revents & POLLIN) {
            if (ctx->hardware) {
                struct input_event input;
                ssize_t size = read(ctx->fd, &input, sizeof(input));
                if (size < 0 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if (size != (ssize_t)sizeof(input)) {
                    AOSL_LOG_ERR("key input disconnected or incomplete event");
                    ctx->emit(MYBOT_KEY_EVENT_EXIT, ctx->user_data);
                    break;
                }
                mybot_key_event_t event;
                if (input.type == EV_KEY && input.value == 1 &&
                    rv1126b_key_translate(input.code, rv1126b_platform_config()->talk_key,
                                          mybot_get_state(), &event)) {
                    ctx->emit(event, ctx->user_data);
                }
            } else {
                char key;
                ssize_t size = read(ctx->fd, &key, 1);
                if (size < 0 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if (size != 1) {
                    AOSL_LOG_ERR("stdin key source closed; stopping device service");
                    ctx->emit(MYBOT_KEY_EVENT_EXIT, ctx->user_data);
                    break;
                }
                stdin_event(ctx, key);
            }
        } else if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            AOSL_LOG_ERR("key source unavailable; stopping device service");
            ctx->emit(MYBOT_KEY_EVENT_EXIT, ctx->user_data);
            break;
        }
    }
    return NULL;
}

static int key_init(void **out, mybot_key_event_handler_t emit, void *user_data) {
    if (!out || !emit) {
        AOSL_LOG_ERR("invalid key listener");
        return -1;
    }
    key_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        AOSL_LOG_ERR("cannot allocate key listener");
        return -1;
    }
    const char *device = rv1126b_platform_config()->input_device;
    ctx->fd = -1;
    ctx->disabled = strcmp(device, "none") == 0;
    if (ctx->disabled) {
        AOSL_LOG_WRN("no input device configured; hardware key controls are disabled");
    }
    ctx->hardware = !ctx->disabled && device[0] != '\0';
    if (!ctx->disabled) {
        ctx->fd = ctx->hardware ? open(device, O_RDONLY | O_NONBLOCK | O_CLOEXEC)
                                : fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3);
    }
    if (!ctx->disabled && ctx->fd < 0) {
        AOSL_LOG_ERR("cannot open key source %s: %s", device[0] ? device : "stdin",
                     strerror(errno));
        free(ctx);
        return -1;
    }
    int version;
    if (ctx->hardware && ioctl(ctx->fd, EVIOCGVERSION, &version) < 0) {
        AOSL_LOG_ERR("%s is not a Linux input event device", device);
        if (ctx->fd >= 0)
            close(ctx->fd);
        free(ctx);
        return -1;
    }
    if (pipe2(ctx->wake, O_CLOEXEC | O_NONBLOCK) < 0) {
        AOSL_LOG_ERR("key wake pipe: %s", strerror(errno));
        if (ctx->fd >= 0)
            close(ctx->fd);
        free(ctx);
        return -1;
    }
    ctx->emit = emit;
    ctx->user_data = user_data;
    pthread_mutex_lock(&s_exit_lock);
    s_exit_requested = false;
    pthread_mutex_unlock(&s_exit_lock);
    int err = pthread_create(&ctx->thread, NULL, key_worker, ctx);
    if (err != 0) {
        AOSL_LOG_ERR("key thread: %s", strerror(err));
        close(ctx->wake[0]);
        close(ctx->wake[1]);
        if (ctx->fd >= 0)
            close(ctx->fd);
        free(ctx);
        return -1;
    }
    *out = ctx;
    return 0;
}

static void key_destroy(void *opaque) {
    key_context_t *ctx = opaque;
    if (!ctx) {
        return;
    }
    close(ctx->wake[1]);
    int err = pthread_join(ctx->thread, NULL);
    if (err != 0) {
        AOSL_LOG_ERR("cannot join key worker: %s", strerror(err));
        abort();
    }
    close(ctx->wake[0]);
    if (ctx->fd >= 0)
        close(ctx->fd);
    free(ctx);
}

const mybot_key_ops_t *rv1126b_key_ops(void) {
    static const mybot_key_ops_t ops = {.init = key_init, .destroy = key_destroy};
    return &ops;
}
