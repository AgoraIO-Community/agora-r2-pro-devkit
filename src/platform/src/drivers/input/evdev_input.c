/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "platform_internal.h"
#include "input_logic.h"
#include <mybot_platform/controls.h>
#include <mybot_platform/display.h>

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
    rv1126b_platform_config_t config;
    bool stopping;
    bool talk_down, talk_long_sent, talk_settings_owned;
    uint32_t talk_pressed_at;
} input_context_t;

typedef struct {
    mybot_key_event_handler_t handler;
    void *user;
} input_attachment_t;

static pthread_mutex_t s_lifecycle_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_page_lock = PTHREAD_MUTEX_INITIALIZER;
static input_context_t *s_context;
static input_attachment_t *s_attachment;
static mybot_settings_page_t s_page;
static int s_exit_requested;
static int s_wifi_requested;
static int s_poweroff_requested;

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

static bool abs_axis(int fd, unsigned code, struct input_absinfo *axis) {
    return ioctl(fd, EVIOCGABS(code), axis) == 0 && axis->maximum > axis->minimum;
}

static bool axis_contains(const struct input_absinfo *axis, int value) {
    return value >= axis->minimum && value <= axis->maximum;
}

static bool read_slot_values(int fd, unsigned code, unsigned count, int32_t values[17]) {
    memset(values, 0, 17 * sizeof(*values));
    values[0] = (int32_t)code;
    return ioctl(fd, EVIOCGMTSLOTS((count + 1) * sizeof(*values)), values) == 0;
}

/* Seed the kernel's retained ABS state without creating a gesture. This also
 * runs after the SYN_REPORT ending a dropped-event interval, as evdev requires. */
static void configure_touch(input_context_t *ctx, unsigned rotation, bool report) {
    if (ctx->touch.fd < 0)
        return;
    rv1126b_touch_t *touch = &ctx->touch_state;
    struct input_absinfo mt_x = {0}, mt_y = {0}, single_x = {0}, single_y = {0};
    bool mt_axes = abs_axis(ctx->touch.fd, ABS_MT_POSITION_X, &mt_x) &&
                   abs_axis(ctx->touch.fd, ABS_MT_POSITION_Y, &mt_y);
    bool single_axes =
        abs_axis(ctx->touch.fd, ABS_X, &single_x) && abs_axis(ctx->touch.fd, ABS_Y, &single_y);
    const struct input_absinfo *x = mt_axes ? &mt_x : &single_x;
    const struct input_absinfo *y = mt_axes ? &mt_y : &single_y;
    rv1126b_touch_configure(touch, x->minimum, x->maximum, y->minimum, y->maximum, rotation);
    touch->mapped = touch->mapped && ctx->config.enable_ui && (mt_axes || single_axes);
    touch->single_x = single_x.value;
    touch->single_y = single_y.value;
    touch->single_x_valid = single_axes && axis_contains(&single_x, single_x.value);
    touch->single_y_valid = single_axes && axis_contains(&single_y, single_y.value);
    memset(touch->slots, 0, sizeof(touch->slots));
    touch->mt_seen = mt_axes;
    touch->slot = 0;
    bool tracking_known = false, active = false;
    if (mt_axes) {
        struct input_absinfo slot_info = {0};
        bool slots_known = ioctl(ctx->touch.fd, EVIOCGABS(ABS_MT_SLOT), &slot_info) == 0 &&
                           slot_info.minimum == 0 && slot_info.maximum >= 0;
        unsigned count =
            slots_known && slot_info.maximum < 16 ? (unsigned)slot_info.maximum + 1 : 16;
        if (slots_known && slot_info.value >= 0 && slot_info.value < 16)
            touch->slot = slot_info.value;
        int32_t values[17];
        bool all_x =
            slots_known && read_slot_values(ctx->touch.fd, ABS_MT_POSITION_X, count, values);
        if (all_x)
            for (unsigned i = 0; i < count; ++i) {
                touch->slots[i].x = values[i + 1];
                touch->slots[i].x_valid = axis_contains(&mt_x, values[i + 1]);
            }
        bool all_y =
            slots_known && read_slot_values(ctx->touch.fd, ABS_MT_POSITION_Y, count, values);
        if (all_y)
            for (unsigned i = 0; i < count; ++i) {
                touch->slots[i].y = values[i + 1];
                touch->slots[i].y_valid = axis_contains(&mt_y, values[i + 1]);
            }
        rv1126b_touch_slot_t *current = &touch->slots[touch->slot];
        if (!all_x) {
            current->x = mt_x.value;
            current->x_valid = axis_contains(&mt_x, mt_x.value);
        }
        if (!all_y) {
            current->y = mt_y.value;
            current->y_valid = axis_contains(&mt_y, mt_y.value);
        }
        if (slots_known && read_slot_values(ctx->touch.fd, ABS_MT_TRACKING_ID, count, values)) {
            tracking_known = slot_info.maximum < 16;
            for (unsigned i = 0; i < count; ++i) {
                touch->slots[i].tracking_id = values[i + 1];
                touch->slots[i].active = values[i + 1] >= 0;
                active = active || touch->slots[i].active;
            }
        }
    }
    unsigned char capabilities[(KEY_MAX + 8) / 8] = {0};
    unsigned char keys[sizeof(capabilities)] = {0};
    bool has_button =
        ioctl(ctx->touch.fd, EVIOCGBIT(EV_KEY, sizeof(capabilities)), capabilities) >= 0 &&
        (capabilities[BTN_TOUCH / 8] & (1U << (BTN_TOUCH % 8)));
    bool button_known = has_button && ioctl(ctx->touch.fd, EVIOCGKEY(sizeof(keys)), keys) >= 0;
    touch->button_seen = button_known;
    touch->button_down = button_known && (keys[BTN_TOUCH / 8] & (1U << (BTN_TOUCH % 8)));
    active = active || touch->button_down;
    touch->down = false;
    touch->await_release = active || (!button_known && !tracking_known);
    touch->release_seen = !touch->await_release;
    touch->blocked = touch->await_release;
    if (report && touch->mapped) {
        AOSL_LOG_NTC("input: touch axes x=%d..%d y=%d..%d rotation=%u", x->minimum, x->maximum,
                     y->minimum, y->maximum, rotation);
    } else if (report && ctx->config.enable_ui) {
        AOSL_LOG_WRN("input: touch axes unavailable; settings touch disabled");
    }
}

static void emit(mybot_key_event_t event) {
    static const char *const names[] = {"conversation_start", "conversation_stop", "pair", "exit",
                                        "volume_up",          "volume_down"};
    if (event == MYBOT_KEY_EVENT_EXIT)
        __atomic_store_n(&s_exit_requested, 1, __ATOMIC_RELEASE);
    /* Holding this lock through the callback makes SDK detach wait for every
     * in-flight dispatch, while the product reader continues after detach. */
    pthread_mutex_lock(&s_callback_lock);
    if (s_attachment) {
        AOSL_LOG_NTC("input: %s", names[event]);
        s_attachment->handler(event, s_attachment->user);
    }
    pthread_mutex_unlock(&s_callback_lock);
}

void rv1126b_input_settings_page(mybot_settings_page_t page) {
    if ((unsigned)page > MYBOT_SETTINGS_POWEROFF_ERROR)
        return;
    pthread_mutex_lock(&s_page_lock);
    s_page = page;
    rv1126b_ui_settings(page);
    pthread_mutex_unlock(&s_page_lock);
}

static mybot_settings_page_t settings_page(void) {
    pthread_mutex_lock(&s_page_lock);
    mybot_settings_page_t page = s_page;
    pthread_mutex_unlock(&s_page_lock);
    return page;
}

/* Caller owns s_page_lock; all UI changes and hit testing share one ordering. */
static void publish_touch(input_context_t *ctx) {
    if (ctx->touch_state.page_changed) {
        ctx->touch_state.page_changed = false;
        s_page = ctx->touch_state.page;
        rv1126b_ui_settings(s_page);
    }
    if (ctx->touch_state.poweroff_pending) {
        ctx->touch_state.poweroff_pending = false;
        __atomic_store_n(&s_poweroff_requested, 1, __ATOMIC_RELEASE);
    }
}

static void stdin_key(char code) {
    if (code == 'w') {
        if (settings_page() != MYBOT_SETTINGS_POWERING_OFF) {
            rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
            __atomic_store_n(&s_wifi_requested, 1, __ATOMIC_RELEASE);
        }
        return;
    }
    if (settings_page() != MYBOT_SETTINGS_CLOSED)
        return;
    mybot_state_t state = mybot_get_state();
    switch (code) {
    case 's':
        if (state == MYBOT_STATE_READY)
            emit(MYBOT_KEY_EVENT_CONVERSATION_START);
        break;
    case 'q':
        if (state == MYBOT_STATE_IN_CONVERSATION)
            emit(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        break;
    case 'p':
        emit(MYBOT_KEY_EVENT_PAIR);
        break;
    case 'u':
        emit(MYBOT_KEY_EVENT_VOLUME_UP);
        break;
    case 'd':
        emit(MYBOT_KEY_EVENT_VOLUME_DOWN);
        break;
    case 'e':
        emit(MYBOT_KEY_EVENT_EXIT);
        break;
    default:
        break;
    }
}

static void back_key(void) {
    mybot_settings_page_t page = settings_page();
    if (page == MYBOT_SETTINGS_MENU)
        rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    else if (page == MYBOT_SETTINGS_CONFIRM_POWEROFF || page == MYBOT_SETTINGS_POWEROFF_ERROR)
        rv1126b_input_settings_page(MYBOT_SETTINGS_MENU);
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
        if (touch) {
            mybot_key_event_t event;
            pthread_mutex_lock(&s_page_lock);
            rv1126b_touch_settings_page(&ctx->touch_state, s_page);
            bool resync =
                ctx->touch_state.dropped && input.type == EV_SYN && input.code == SYN_REPORT;
            int ready = rv1126b_touch_event(&ctx->touch_state, input.type, input.code, input.value,
                                            now_ms(), mybot_get_state(), &event);
            if (resync)
                configure_touch(ctx, ctx->touch_state.rotation, false);
            publish_touch(ctx);
            pthread_mutex_unlock(&s_page_lock);
            if (ready)
                emit(event);
            continue;
        }
        if (input.type == EV_SYN && input.code == SYN_DROPPED) {
            ctx->talk_down = false;
            ctx->talk_long_sent = false;
        }
        if (input.type != EV_KEY)
            continue;
        if (input.code == KEY_ESC && input.value == 1) {
            back_key();
            continue;
        }
        if (input.code == ctx->config.talk_key) {
            if (input.value == 1 && !ctx->talk_down) {
                ctx->talk_down = true;
                ctx->talk_long_sent = false;
                ctx->talk_settings_owned = settings_page() != MYBOT_SETTINGS_CLOSED;
                ctx->talk_pressed_at = now_ms();
            } else if (input.value == 0) {
                if (ctx->talk_down && !ctx->talk_long_sent && !ctx->talk_settings_owned &&
                    settings_page() == MYBOT_SETTINGS_CLOSED) {
                    if ((uint32_t)(now_ms() - ctx->talk_pressed_at) >= 3000) {
                        __atomic_store_n(&s_wifi_requested, 1, __ATOMIC_RELEASE);
                    } else {
                        mybot_key_event_t event;
                        if (rv1126b_key_translate(input.code, ctx->config.talk_key,
                                                  mybot_get_state(), &event))
                            emit(event);
                    }
                }
                ctx->talk_down = false;
            }
            continue;
        }
        mybot_key_event_t event;
        if (input.value == 1 && settings_page() == MYBOT_SETTINGS_CLOSED &&
            rv1126b_key_translate(input.code, ctx->config.talk_key, mybot_get_state(), &event))
            emit(event);
    }
    return 0;
}

static void poll_gestures(input_context_t *ctx) {
    uint32_t now = now_ms();
    pthread_mutex_lock(&s_page_lock);
    rv1126b_touch_settings_page(&ctx->touch_state, s_page);
    if (rv1126b_touch_provisioning_request(&ctx->touch_state, now))
        __atomic_store_n(&s_wifi_requested, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&s_page_lock);
    if (ctx->talk_down && !ctx->talk_long_sent && !ctx->talk_settings_owned &&
        settings_page() == MYBOT_SETTINGS_CLOSED &&
        (uint32_t)(now - ctx->talk_pressed_at) >= 3000) {
        ctx->talk_long_sent = true;
        __atomic_store_n(&s_wifi_requested, 1, __ATOMIC_RELEASE);
    }
}

static void *input_worker(void *opaque) {
    input_context_t *ctx = opaque;
    while (!__atomic_load_n(&ctx->stopping, __ATOMIC_ACQUIRE)) {
        struct pollfd fds[4] = {{ctx->wake[0], POLLIN, 0},
                                {ctx->stdin_fd, POLLIN, 0},
                                {ctx->key.fd, POLLIN, 0},
                                {ctx->touch.fd, POLLIN, 0}};
        int ret = poll(fds, 4, 100);
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
                    stdin_key(data[i]);
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
                if (i == 0)
                    ctx->talk_down = false;
                else
                    memset(&ctx->touch_state, 0, sizeof(ctx->touch_state));
            }
        }
        poll_gestures(ctx);
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

int rv1126b_input_start(const rv1126b_platform_config_t *config, unsigned rotation) {
    if (!config || !config->input_device || !config->touch_device || rotation > 270 ||
        rotation % 90)
        return -1;
    pthread_mutex_lock(&s_lifecycle_lock);
    if (s_context) {
        pthread_mutex_unlock(&s_lifecycle_lock);
        return -1;
    }
    input_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        pthread_mutex_unlock(&s_lifecycle_lock);
        return -1;
    }
    ctx->stdin_fd = ctx->key.fd = ctx->touch.fd = ctx->wake[0] = ctx->wake[1] = -1;
    ctx->config = *config;
    ctx->key.fd = open_input(config->input_device, "adc-keys");
    ctx->touch.fd = open_input(config->touch_device, "axs_ts");
    const char *paths[2] = {config->input_device, config->touch_device};
    const int descriptors[2] = {ctx->key.fd, ctx->touch.fd};
    for (int i = 0; i < 2; ++i) {
        if (paths[i][0] && strcmp(paths[i], "auto") && strcmp(paths[i], "none") &&
            strcmp(paths[i], "stdin") && descriptors[i] < 0) {
            AOSL_LOG_ERR("input: cannot open requested device %s", paths[i]);
            close_context(ctx);
            pthread_mutex_unlock(&s_lifecycle_lock);
            return -1;
        }
    }
    configure_touch(ctx, rotation, true);
    bool explicit_stdin = !config->input_device[0] || strcmp(config->input_device, "stdin") == 0;
    if (explicit_stdin || (isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) == getpgrp()))
        ctx->stdin_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3);
    if (pipe2(ctx->wake, O_CLOEXEC | O_NONBLOCK) < 0) {
        close_context(ctx);
        pthread_mutex_unlock(&s_lifecycle_lock);
        return -1;
    }
    __atomic_store_n(&s_exit_requested, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_wifi_requested, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_poweroff_requested, 0, __ATOMIC_RELEASE);
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    if (pthread_create(&ctx->thread, NULL, input_worker, ctx) != 0) {
        close_context(ctx);
        pthread_mutex_unlock(&s_lifecycle_lock);
        return -1;
    }
    s_context = ctx;
    pthread_mutex_unlock(&s_lifecycle_lock);
    AOSL_LOG_NTC("input: product reader ready; settings available before SDK startup; "
                 "short touch/menu toggles conversation; hold touch/menu 3 s for Wi-Fi");
    return 0;
}

int rv1126b_input_stop(void) {
    pthread_mutex_lock(&s_lifecycle_lock);
    input_context_t *ctx = s_context;
    if (!ctx) {
        pthread_mutex_unlock(&s_lifecycle_lock);
        return 0;
    }
    __atomic_store_n(&ctx->stopping, true, __ATOMIC_RELEASE);
    if (ctx->wake[1] >= 0) {
        const char byte = 1;
        (void)write(ctx->wake[1], &byte, 1);
    }
    if (pthread_join(ctx->thread, NULL) != 0) {
        AOSL_LOG_ERR("input: join failed; retaining context");
        pthread_mutex_unlock(&s_lifecycle_lock);
        return -1;
    }
    pthread_mutex_lock(&s_callback_lock);
    s_attachment = NULL;
    pthread_mutex_unlock(&s_callback_lock);
    close_context(ctx);
    s_context = NULL;
    pthread_mutex_unlock(&s_lifecycle_lock);
    return 0;
}

static int key_init(void **out, mybot_key_event_handler_t handler, void *user) {
    if (!out || !handler)
        return -1;
    input_attachment_t *attachment = malloc(sizeof(*attachment));
    if (!attachment)
        return -1;
    attachment->handler = handler;
    attachment->user = user;
    pthread_mutex_lock(&s_lifecycle_lock);
    pthread_mutex_lock(&s_callback_lock);
    if (!s_context || s_attachment) {
        pthread_mutex_unlock(&s_callback_lock);
        pthread_mutex_unlock(&s_lifecycle_lock);
        free(attachment);
        return -1;
    }
    s_attachment = attachment;
    *out = attachment;
    pthread_mutex_unlock(&s_callback_lock);
    pthread_mutex_unlock(&s_lifecycle_lock);
    return 0;
}

static void key_destroy(void *opaque) {
    if (!opaque)
        return;
    pthread_mutex_lock(&s_callback_lock);
    if (s_attachment == opaque)
        s_attachment = NULL;
    pthread_mutex_unlock(&s_callback_lock);
    free(opaque);
}

int rv1126b_key_exit_requested(void) {
    return __atomic_load_n(&s_exit_requested, __ATOMIC_ACQUIRE);
}

int rv1126b_key_wifi_requested(void) {
    return __atomic_exchange_n(&s_wifi_requested, 0, __ATOMIC_ACQ_REL);
}

int rv1126b_key_poweroff_requested(void) {
    return __atomic_exchange_n(&s_poweroff_requested, 0, __ATOMIC_ACQ_REL);
}

const mybot_key_ops_t *rv1126b_key_ops(void) {
    static const mybot_key_ops_t ops = {.init = key_init, .destroy = key_destroy};
    return &ops;
}
