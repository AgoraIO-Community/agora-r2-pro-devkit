/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/prompt.h"
#include "mybot_platform/audio.h"
#include "check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char assets[] = "/tmp/mybot-prompt-test-XXXXXX";
static char chinese[512], english[512];
static int pcm;
static int init_calls, start_calls, stop_calls, destroy_calls, drain_calls;
static int fail_init, fail_start, fail_stop, fail_drain;
static int writes[16];
static size_t write_count, write_index;
static int16_t accepted_samples[2048];
static size_t accepted_count;
static unsigned int write_calls;
static bool zero_writes;

static uint64_t monotonic_ms(void) {
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void pause_one_ms(void) {
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
    nanosleep(&pause, NULL);
}

static void wait_finished(void) {
    uint64_t deadline = monotonic_ms() + 2000;
    while (rv1126b_prompt_active() && monotonic_ms() < deadline) {
        pause_one_ms();
    }
    CHECK(!rv1126b_prompt_active());
}

static int playback_init(void **context, int rate, int channels, int bits) {
    CHECK(context && rate == 16000 && channels == 1 && bits == 16);
    ++init_calls;
    CHECK(pcm == 0);
    if (fail_init) {
        return -1;
    }
    pcm = 1;
    *context = &pcm;
    return 0;
}

static int playback_start(void *context) {
    CHECK(context == &pcm && pcm == 1);
    ++start_calls;
    return fail_start ? -1 : 0;
}

static int playback_write(void *context, const void *buffer, int frames) {
    CHECK(context == &pcm && pcm == 1 && frames > 0 && frames <= 320);
    __atomic_add_fetch(&write_calls, 1, __ATOMIC_RELEASE);
    int accepted = zero_writes ? 0 : frames;
    if (write_index < write_count) {
        accepted = writes[write_index++];
    }
    if (accepted > 0 && accepted <= frames) {
        CHECK(accepted_count + (size_t)accepted <=
              sizeof(accepted_samples) / sizeof(*accepted_samples));
        memcpy(accepted_samples + accepted_count, buffer, (size_t)accepted * sizeof(int16_t));
        accepted_count += (size_t)accepted;
    }
    return accepted;
}

static int playback_stop(void *context) {
    CHECK(context == &pcm && pcm == 1);
    ++stop_calls;
    return fail_stop ? -1 : 0;
}

static void playback_destroy(void *context) {
    CHECK(context == &pcm && pcm == 1);
    ++destroy_calls;
    pcm = 0;
}

const mybot_audio_playback_ops_t *rv1126b_playback_ops(void) {
    static const mybot_audio_playback_ops_t operations = {
        .init = playback_init,
        .start = playback_start,
        .write = playback_write,
        .stop = playback_stop,
        .destroy = playback_destroy,
    };
    return &operations;
}

int rv1126b_playback_drain(void *context, unsigned int timeout_ms) {
    CHECK(context == &pcm && pcm == 1 && timeout_ms == 100);
    ++drain_calls;
    return fail_drain ? -1 : 0;
}

static void reset_mock(void) {
    rv1126b_prompt_stop();
    CHECK(pcm == 0);
    init_calls = start_calls = stop_calls = destroy_calls = drain_calls = 0;
    fail_init = fail_start = fail_stop = fail_drain = 0;
    write_count = write_index = accepted_count = 0;
    zero_writes = false;
    __atomic_store_n(&write_calls, 0, __ATOMIC_RELEASE);
}

static void write_asset(const char *path, int offset) {
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(fd >= 0);
    /* One complete 20 ms frame plus a short EOF tail, encoded as S16LE. */
    for (int i = 0; i < 340; ++i) {
        int16_t sample = (int16_t)(i * 31 - 12000 + offset);
        uint16_t raw = (uint16_t)sample;
        unsigned char bytes[] = {(unsigned char)raw, (unsigned char)(raw >> 8)};
        CHECK(write(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    }
    CHECK(close(fd) == 0);
}

static void check_cleanup(int initializations, int starts, int stops, int destroys, int drains) {
    CHECK(init_calls == initializations && start_calls == starts && stop_calls == stops &&
          destroy_calls == destroys && drain_calls == drains && pcm == 0);
}

static void test_locale_and_partial_writes(void) {
    const char *locales[] = {"zh-CN", "en-US"};
    for (int locale = 0; locale < 2; ++locale) {
        reset_mock();
        const int script[] = {100, 0, 73, 147, 20};
        memcpy(writes, script, sizeof(script));
        write_count = sizeof(script) / sizeof(*script);
        CHECK(rv1126b_prompt_start(assets, locales[locale]) == 0);
        wait_finished();
        CHECK(accepted_count == 340 && write_index == write_count);
        for (size_t i = 0; i < accepted_count; ++i) {
            CHECK(accepted_samples[i] == (int16_t)((int)i * 31 - 12000 + locale * 20000));
        }
        check_cleanup(1, 1, 1, 1, 1);
        /* Completion retains a joinable worker until the product reaps it. */
        CHECK(rv1126b_prompt_start(assets, locales[locale]) < 0 && errno == EBUSY);
        rv1126b_prompt_stop();
        rv1126b_prompt_stop();
    }
}

static void test_playback_failures_and_stall(void) {
    reset_mock();
    fail_init = 1;
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    check_cleanup(1, 0, 0, 0, 0);
    reset_mock();
    fail_start = 1;
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    check_cleanup(1, 1, 0, 1, 0);
    reset_mock();
    writes[0] = 100;
    writes[1] = -1;
    write_count = 2;
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    CHECK(accepted_count == 100);
    check_cleanup(1, 1, 1, 1, 0);
    reset_mock();
    writes[0] = 321;
    write_count = 1;
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    check_cleanup(1, 1, 1, 1, 0);
    reset_mock();
    fail_drain = fail_stop = 1;
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    check_cleanup(1, 1, 1, 1, 1);
    reset_mock();
    zero_writes = true;
    uint64_t started = monotonic_ms();
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    CHECK(monotonic_ms() - started >= 200 && monotonic_ms() - started < 1500);
    check_cleanup(1, 1, 1, 1, 0);
}

static void test_cancel_and_restart(void) {
    reset_mock();
    zero_writes = true;
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    uint64_t deadline = monotonic_ms() + 1000;
    while (__atomic_load_n(&write_calls, __ATOMIC_ACQUIRE) == 0 && monotonic_ms() < deadline) {
        pause_one_ms();
    }
    CHECK(__atomic_load_n(&write_calls, __ATOMIC_ACQUIRE) > 0);
    CHECK(rv1126b_prompt_start(assets, "en-US") < 0 && errno == EBUSY);
    uint64_t started = monotonic_ms();
    rv1126b_prompt_stop();
    CHECK(monotonic_ms() - started < 500);
    CHECK(!rv1126b_prompt_active());
    check_cleanup(1, 1, 1, 1, 0);
    reset_mock();
    CHECK(rv1126b_prompt_start(assets, "en-US") == 0);
    wait_finished();
    check_cleanup(1, 1, 1, 1, 1);
}

static void check_invalid_asset(void) {
    reset_mock();
    CHECK(rv1126b_prompt_start(assets, "zh-CN") == 0);
    wait_finished();
    check_cleanup(0, 0, 0, 0, 0);
    rv1126b_prompt_stop();
}

static void test_invalid_assets_and_arguments(void) {
    reset_mock();
    CHECK(rv1126b_prompt_start(NULL, "zh-CN") < 0);
    CHECK(rv1126b_prompt_start("", "zh-CN") < 0);
    CHECK(rv1126b_prompt_start(assets, NULL) < 0);
    CHECK(rv1126b_prompt_start(assets, "fr-FR") < 0);
    CHECK(rv1126b_prompt_start(assets, "../en-US") < 0);
    char long_path[8192];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    CHECK(rv1126b_prompt_start(long_path, "zh-CN") < 0);
    CHECK(!rv1126b_prompt_active());
    CHECK(unlink(chinese) == 0);
    check_invalid_asset();
    CHECK(mkdir(chinese, 0700) == 0);
    check_invalid_asset();
    CHECK(rmdir(chinese) == 0);
    CHECK(mkfifo(chinese, 0600) == 0);
    check_invalid_asset();
    CHECK(unlink(chinese) == 0);
    CHECK(symlink(english, chinese) == 0);
    check_invalid_asset();
    CHECK(unlink(chinese) == 0);
    int fd = open(chinese, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(close(fd) == 0);
    check_invalid_asset();
    fd = open(chinese, O_WRONLY);
    CHECK(fd >= 0 && write(fd, "x", 1) == 1 && close(fd) == 0);
    check_invalid_asset();
    fd = open(chinese, O_WRONLY);
    CHECK(fd >= 0 && ftruncate(fd, 1024 * 1024 + 2) == 0 && close(fd) == 0);
    check_invalid_asset();
    write_asset(chinese, 0);
}

int main(void) {
    CHECK(mkdtemp(assets));
    char path[512];
    CHECK(snprintf(path, sizeof(path), "%s/locales", assets) > 0);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/locales/zh-CN", assets) > 0);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/locales/en-US", assets) > 0);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(snprintf(chinese, sizeof(chinese), "%s/locales/zh-CN/wificonfig.pcm", assets) > 0);
    CHECK(snprintf(english, sizeof(english), "%s/locales/en-US/wificonfig.pcm", assets) > 0);
    write_asset(chinese, 0);
    write_asset(english, 20000);
    test_locale_and_partial_writes();
    test_playback_failures_and_stall();
    test_cancel_and_restart();
    test_invalid_assets_and_arguments();
    reset_mock();
    CHECK(unlink(chinese) == 0 && unlink(english) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/locales/zh-CN", assets) > 0 && rmdir(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/locales/en-US", assets) > 0 && rmdir(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/locales", assets) > 0 && rmdir(path) == 0);
    CHECK(rmdir(assets) == 0);
    puts("Locale PCM prompt, partial writes, cancellation, failures and playback ownership: PASS");
    return 0;
}
