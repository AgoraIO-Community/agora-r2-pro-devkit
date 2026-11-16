/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>

static int native_pipe2(int descriptors[2], int flags) {
    (void)flags;
    if (pipe(descriptors))
        return -1;
    for (int i = 0; i < 2; ++i) {
        if (fcntl(descriptors[i], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(descriptors[i], F_SETFL, O_NONBLOCK) < 0) {
            close(descriptors[0]);
            close(descriptors[1]);
            return -1;
        }
    }
    return 0;
}

#define pipe2 native_pipe2
static int input_ioctl(int fd, unsigned long request, ...);

#define ioctl input_ioctl
#include "../../src/platform/src/drivers/input/evdev_input.c"
#undef ioctl
#include "check.h"

static struct {
    bool enabled, button_down, slots_supported;
    int fd, selected_slot;
    int x[2], y[2], tracking[2];
} kernel_touch;

/* Each kernel MT slot retains its axes even while its tracking ID is -1. */
static int input_ioctl(int fd, unsigned long request, ...) {
    if (!kernel_touch.enabled || fd != kernel_touch.fd) {
        errno = ENOTTY;
        return -1;
    }
    va_list arguments;
    va_start(arguments, request);
    void *out = va_arg(arguments, void *);
    va_end(arguments);
    const size_t key_bytes = (KEY_MAX + 8) / 8;
    if (request == EVIOCGBIT(EV_KEY, key_bytes) || request == EVIOCGKEY(key_bytes)) {
        unsigned char *bits = out;
        memset(bits, 0, key_bytes);
        if (request == EVIOCGBIT(EV_KEY, key_bytes) || kernel_touch.button_down)
            bits[BTN_TOUCH / 8] |= 1U << (BTN_TOUCH % 8);
        return 0;
    }
    if (request == EVIOCGMTSLOTS(3 * sizeof(int32_t)) && kernel_touch.slots_supported) {
        int32_t *values = out;
        for (int i = 0; i < 2; ++i) {
            if (values[0] == ABS_MT_POSITION_X)
                values[i + 1] = kernel_touch.x[i];
            else if (values[0] == ABS_MT_POSITION_Y)
                values[i + 1] = kernel_touch.y[i];
            else if (values[0] == ABS_MT_TRACKING_ID)
                values[i + 1] = kernel_touch.tracking[i];
            else
                return -1;
        }
        return 0;
    }
    struct input_absinfo *axis = out;
    memset(axis, 0, sizeof(*axis));
    int slot = kernel_touch.selected_slot;
    if (request == EVIOCGABS(ABS_MT_SLOT)) {
        axis->maximum = 1;
        axis->value = slot;
    } else if (request == EVIOCGABS(ABS_MT_POSITION_X) || request == EVIOCGABS(ABS_X)) {
        axis->maximum = 639;
        axis->value = kernel_touch.x[slot];
    } else if (request == EVIOCGABS(ABS_MT_POSITION_Y) || request == EVIOCGABS(ABS_Y)) {
        axis->maximum = 479;
        axis->value = kernel_touch.y[slot];
    } else {
        errno = ENOTTY;
        return -1;
    }
    return 0;
}

static const rv1126b_platform_config_t config = {
    .input_device = "stdin",
    .touch_device = "none",
    .talk_key = KEY_MENU,
};

const rv1126b_platform_config_t *rv1126b_platform_config(void) {
    return &config;
}

mybot_state_t mybot_get_state(void) {
    return MYBOT_STATE_READY;
}

void rv1126b_ui_settings(mybot_settings_page_t page) {
    (void)page;
}

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool callback_entered, release_callback, destroyed;
static unsigned calls;
static unsigned menu_calls;

static void menu_handler(mybot_key_event_t event, void *user) {
    (void)user;
    CHECK(event == MYBOT_KEY_EVENT_CONVERSATION_START);
    ++menu_calls;
}

static void menu_edge(input_context_t *context, input_source_t *source, int write_fd, int value) {
    const struct input_event event = {.type = EV_KEY, .code = KEY_MENU, .value = value};
    CHECK(write(write_fd, &event, sizeof(event)) == (ssize_t)sizeof(event));
    CHECK(drain_input(context, source, false) == 0);
}

static void check_menu_gestures(void) {
    int descriptors[2];
    CHECK(native_pipe2(descriptors, 0) == 0);
    input_context_t context = {.config = config};
    input_attachment_t attachment = {.handler = menu_handler};
    s_attachment = &attachment;
    input_source_t source = {.fd = descriptors[0]};
    __atomic_store_n(&s_wifi_requested, 0, __ATOMIC_RELEASE);
    menu_edge(&context, &source, descriptors[1], 1);
    CHECK(menu_calls == 0);
    menu_edge(&context, &source, descriptors[1], 0);
    CHECK(menu_calls == 1 && !rv1126b_key_wifi_requested());
    menu_edge(&context, &source, descriptors[1], 1);
    context.talk_pressed_at = now_ms() - 3000;
    menu_edge(&context, &source, descriptors[1], 2);
    menu_edge(&context, &source, descriptors[1], 0);
    CHECK(menu_calls == 1 && rv1126b_key_wifi_requested());
    CHECK(!rv1126b_key_wifi_requested());
    menu_edge(&context, &source, descriptors[1], 1);
    context.talk_long_sent = true;
    menu_edge(&context, &source, descriptors[1], 0);
    CHECK(menu_calls == 1 && !rv1126b_key_wifi_requested());
    rv1126b_input_settings_page(MYBOT_SETTINGS_MENU);
    menu_edge(&context, &source, descriptors[1], 1);
    context.talk_pressed_at = now_ms() - 3000;
    poll_gestures(&context);
    menu_edge(&context, &source, descriptors[1], 0);
    CHECK(menu_calls == 1 && !rv1126b_key_wifi_requested());
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    s_attachment = NULL;
    close(descriptors[0]);
    close(descriptors[1]);
}

static void touch_edge(input_context_t *context, input_source_t *source, int fd, unsigned type,
                       unsigned code, int value) {
    const struct input_event event = {.type = type, .code = code, .value = value};
    CHECK(write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event));
    CHECK(drain_input(context, source, true) == 0);
}

static void touch_contact(input_context_t *context, input_source_t *source, int fd, int id, int x,
                          int y, bool update_x, bool update_y) {
    touch_edge(context, source, fd, EV_ABS, ABS_MT_SLOT, 0);
    touch_edge(context, source, fd, EV_ABS, ABS_MT_TRACKING_ID, id);
    if (update_x)
        touch_edge(context, source, fd, EV_ABS, ABS_MT_POSITION_X, x);
    if (update_y)
        touch_edge(context, source, fd, EV_ABS, ABS_MT_POSITION_Y, y);
    touch_edge(context, source, fd, EV_KEY, BTN_TOUCH, 1);
    touch_edge(context, source, fd, EV_SYN, SYN_REPORT, 0);
    context->touch_state.pressed_at = now_ms() - 100;
    touch_edge(context, source, fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    touch_edge(context, source, fd, EV_KEY, BTN_TOUCH, 0);
    touch_edge(context, source, fd, EV_SYN, SYN_REPORT, 0);
}

static void check_retained_mt_axes(void) {
    int descriptors[2];
    CHECK(native_pipe2(descriptors, 0) == 0);
    input_context_t context = {.config = config};
    input_source_t source = {.fd = descriptors[0]};
    rv1126b_touch_configure(&context.touch_state, 0, 639, 0, 479, 0);
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    /* A never-initialized slot still cannot borrow another contact's position. */
    touch_contact(&context, &source, descriptors[1], 0, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_CLOSED);
    /* Protocol B retains each slot's ABS values. The kernel omits coordinates
     * which have not changed since the preceding contact, including both axes. */
    touch_contact(&context, &source, descriptors[1], 1, 596, 44, true, true);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);
    touch_contact(&context, &source, descriptors[1], 2, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_CLOSED);
    touch_contact(&context, &source, descriptors[1], 3, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);
    touch_contact(&context, &source, descriptors[1], 4, 200, 270, true, true);
    CHECK(settings_page() == MYBOT_SETTINGS_CONFIRM_POWEROFF);
    touch_contact(&context, &source, descriptors[1], 5, 200, 330, false, true);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU && !rv1126b_key_poweroff_requested());
    touch_contact(&context, &source, descriptors[1], 6, 200, 180, false, true);
    CHECK(settings_page() == MYBOT_SETTINGS_CLOSED);
    poll_gestures(&context);
    CHECK(rv1126b_key_wifi_requested() && !rv1126b_key_wifi_requested());
    touch_contact(&context, &source, descriptors[1], 7, 640, 180, true, false);
    CHECK(settings_page() == MYBOT_SETTINGS_CLOSED);
    touch_contact(&context, &source, descriptors[1], 8, 596, 44, true, true);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    close(descriptors[0]);
    close(descriptors[1]);
}

static void check_kernel_touch_seed(void) {
    int descriptors[2];
    CHECK(native_pipe2(descriptors, 0) == 0);
    input_context_t context = {.config = config, .touch.fd = descriptors[0]};
    context.config.enable_ui = true;
    input_source_t source = {.fd = descriptors[0]};
    kernel_touch.enabled = true;
    kernel_touch.slots_supported = true;
    kernel_touch.fd = descriptors[0];
    kernel_touch.selected_slot = 1;
    kernel_touch.x[0] = 596;
    kernel_touch.y[0] = 44;
    kernel_touch.x[1] = 200;
    kernel_touch.y[1] = 180;
    kernel_touch.tracking[0] = kernel_touch.tracking[1] = -1;
    configure_touch(&context, 0, false);
    CHECK(context.touch_state.slot == 1 && !context.touch_state.await_release);
    CHECK(context.touch_state.slots[0].x_valid && context.touch_state.slots[1].y_valid);
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    /* The first press after restart contains neither axis, on a different slot
     * from the current slot returned by EVIOCGABS. */
    touch_contact(&context, &source, descriptors[1], 1, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);

    /* Recovery queries only after the report ending SYN_DROPPED. A held
     * contact must be released before another gesture can execute. */
    kernel_touch.selected_slot = 0;
    kernel_touch.tracking[0] = 2;
    kernel_touch.button_down = true;
    touch_edge(&context, &source, descriptors[1], EV_SYN, SYN_DROPPED, 0);
    touch_edge(&context, &source, descriptors[1], EV_SYN, SYN_REPORT, 0);
    CHECK(context.touch_state.await_release && !context.touch_state.down);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);
    touch_edge(&context, &source, descriptors[1], EV_ABS, ABS_MT_TRACKING_ID, -1);
    touch_edge(&context, &source, descriptors[1], EV_KEY, BTN_TOUCH, 0);
    touch_edge(&context, &source, descriptors[1], EV_SYN, SYN_REPORT, 0);
    CHECK(!context.touch_state.await_release && settings_page() == MYBOT_SETTINGS_MENU);
    touch_contact(&context, &source, descriptors[1], 3, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_CLOSED);

    kernel_touch.tracking[0] = -1;
    kernel_touch.button_down = false;
    touch_edge(&context, &source, descriptors[1], EV_SYN, SYN_DROPPED, 0);
    touch_edge(&context, &source, descriptors[1], EV_SYN, SYN_REPORT, 0);
    CHECK(!context.touch_state.await_release);
    touch_contact(&context, &source, descriptors[1], 4, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);

    /* A driver lacking MTSLOTS still seeds its selected slot's ABS values. */
    kernel_touch.slots_supported = false;
    configure_touch(&context, 0, false);
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    touch_contact(&context, &source, descriptors[1], 5, 596, 44, false, false);
    CHECK(settings_page() == MYBOT_SETTINGS_MENU);
    rv1126b_input_settings_page(MYBOT_SETTINGS_CLOSED);
    kernel_touch.enabled = false;
    close(descriptors[0]);
    close(descriptors[1]);
}

static void handler(mybot_key_event_t event, void *user) {
    CHECK(user == &calls && event == MYBOT_KEY_EVENT_PAIR);
    pthread_mutex_lock(&lock);
    ++calls;
    callback_entered = true;
    pthread_cond_broadcast(&changed);
    while (!release_callback)
        pthread_cond_wait(&changed, &lock);
    pthread_mutex_unlock(&lock);
}

static void *destroy_worker(void *ctx) {
    rv1126b_key_ops()->destroy(ctx);
    pthread_mutex_lock(&lock);
    destroyed = true;
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void wait_wifi(void) {
    bool requested = false;
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
    for (unsigned attempt = 0; attempt < 1000 && !requested; ++attempt) {
        requested = rv1126b_key_wifi_requested() != 0;
        if (!requested)
            nanosleep(&pause, NULL);
    }
    CHECK(requested && !rv1126b_key_wifi_requested());
}

int main(void) {
    check_menu_gestures();
    check_retained_mt_axes();
    check_kernel_touch_seed();
    int input[2];
    CHECK(pipe(input) == 0);
    int saved = dup(STDIN_FILENO);
    CHECK(saved >= 0 && dup2(input[0], STDIN_FILENO) >= 0);
    void *ctx = NULL;
    CHECK(rv1126b_key_ops()->init(&ctx, handler, &calls) < 0);
    for (int i = 0; i < 5; ++i) {
        CHECK(rv1126b_input_start(&config, 270) == 0);
        CHECK(rv1126b_key_ops()->init(&ctx, handler, &calls) == 0);
        uint32_t started = now_ms();
        rv1126b_key_ops()->destroy(ctx);
        CHECK(rv1126b_input_stop() == 0);
        CHECK((uint32_t)(now_ms() - started) < 1000);
    }
    CHECK(rv1126b_input_start(&config, 270) == 0);
    CHECK(rv1126b_input_start(&config, 270) < 0);
    input_context_t *reader = s_context;
    CHECK(write(input[1], "w", 1) == 1);
    wait_wifi();
    CHECK(calls == 0);
    CHECK(rv1126b_key_ops()->init(&ctx, handler, &calls) == 0);
    void *duplicate = NULL;
    CHECK(rv1126b_key_ops()->init(&duplicate, handler, &calls) < 0);
    CHECK(s_context == reader);
    CHECK(write(input[1], "p", 1) == 1);
    struct timespec deadline;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&lock);
    while (!callback_entered)
        CHECK(pthread_cond_timedwait(&changed, &lock, &deadline) == 0);
    pthread_mutex_unlock(&lock);
    pthread_t stopping;
    CHECK(pthread_create(&stopping, NULL, destroy_worker, ctx) == 0);
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 30000000};
    nanosleep(&pause, NULL);
    pthread_mutex_lock(&lock);
    CHECK(!destroyed);
    release_callback = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    CHECK(pthread_join(stopping, NULL) == 0);
    CHECK(destroyed && calls == 1 && s_context == reader);
    CHECK(write(input[1], "p", 1) == 1);
    nanosleep(&pause, NULL);
    CHECK(calls == 1);
    /* Detaching the SDK leaves product provisioning available on the same reader. */
    CHECK(write(input[1], "w", 1) == 1);
    wait_wifi();
    CHECK(rv1126b_input_stop() == 0);
    CHECK(rv1126b_input_stop() == 0);
    CHECK(dup2(saved, STDIN_FILENO) >= 0);
    close(saved);
    close(input[0]);
    close(input[1]);
    puts("Input product lifecycle, settings isolation and synchronized SDK detach: PASS");
    return 0;
}
