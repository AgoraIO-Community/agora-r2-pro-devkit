/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "platform_internal.h"
#include "mybot_platform/volume.h"
#include "check.h"

#include <alsa/asoundlib.h>
#include <api/aosl_log.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mybot/mybot_build_config.h>

static int allocations, mutexes, handles, init_step, fail_step;
static int fail_alloc, fail_mutex, prepare_error, drop_error, negotiated_rate = 16000;
static unsigned read_calls, write_calls, prepare_calls;
static uint64_t fake_ms = 1000;
static snd_pcm_t hardware;
static snd_pcm_sframes_t read_script[8], write_script[8];
static size_t read_count, read_index, write_count, write_index;
static int16_t written_samples[640];
static size_t written_count;
static int drain_error;
static unsigned drain_calls, drain_again;
static void *stop_during_wait;
static const rv1126b_platform_config_t config = {.capture_device = "mock-capture",
                                                 .playback_device = "mock-playback"};

const rv1126b_platform_config_t *rv1126b_platform_config(void) {
    return &config;
}

static void *audio_calloc(size_t count, size_t size) {
    if (fail_alloc > 0 && --fail_alloc == 0)
        return NULL;
    void *result = calloc(count, size);
    if (result)
        ++allocations;
    return result;
}
static void *audio_realloc(void *pointer, size_t size) {
    bool was_null = pointer == NULL;
    void *result = realloc(pointer, size);
    if (result && was_null)
        ++allocations;
    return result;
}
static void audio_free(void *pointer) {
    if (pointer)
        --allocations;
    free(pointer);
}
static int audio_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes) {
    if (fail_mutex)
        return EAGAIN;
    int result = pthread_mutex_init(mutex, attributes);
    if (!result)
        ++mutexes;
    return result;
}
static int audio_mutex_destroy(pthread_mutex_t *mutex) {
    int result = pthread_mutex_destroy(mutex);
    CHECK(result == 0);
    --mutexes;
    return result;
}
static int audio_clock_gettime(clockid_t clock, struct timespec *time) {
    CHECK(clock == CLOCK_MONOTONIC);
    time->tv_sec = (time_t)(fake_ms / 1000);
    time->tv_nsec = (long)(fake_ms % 1000) * 1000000;
    ++fake_ms;
    return 0;
}
static int audio_nanosleep(const struct timespec *requested, struct timespec *remaining) {
    (void)remaining;
    CHECK(requested->tv_sec == 0 && requested->tv_nsec == 1000000);
    ++fake_ms;
    if (stop_during_wait) {
        void *ctx = stop_during_wait;
        stop_during_wait = NULL;
        /* Models another caller acquiring the lock while nonblocking I/O waits.
         * Holding that lock across the wait would deadlock this test. */
        CHECK(rv1126b_capture_ops()->stop(ctx) == 0);
    }
    return 0;
}

#define calloc audio_calloc
#define realloc audio_realloc
#define free audio_free
#define pthread_mutex_init audio_mutex_init
#define pthread_mutex_destroy audio_mutex_destroy
#define clock_gettime audio_clock_gettime
#define nanosleep audio_nanosleep
#include "../../src/platform/src/drivers/audio/alsa_audio.c"
#undef calloc
#undef realloc
#undef free
#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef clock_gettime
#undef nanosleep

static int configure_result(void) {
    return ++init_step == fail_step ? -EIO : 0;
}
int snd_pcm_open(snd_pcm_t **pcm, const char *device, snd_pcm_stream_t stream, int flags) {
    CHECK(strcmp(device, stream == SND_PCM_STREAM_CAPTURE ? "mock-capture" : "mock-playback") == 0);
    CHECK(flags == SND_PCM_NONBLOCK && handles == 0);
    int result = configure_result();
    if (!result) {
        hardware.open = 1;
        *pcm = &hardware;
        ++handles;
    }
    return result;
}
int snd_pcm_close(snd_pcm_t *pcm) {
    CHECK(pcm == &hardware && pcm->open && handles == 1);
    pcm->open = 0;
    --handles;
    return 0;
}
const char *snd_strerror(int error) {
    (void)error;
    return "injected ALSA error";
}
#define CHECK_PCM_PARAMS()                                                                         \
    do {                                                                                           \
        CHECK(pcm == &hardware && pcm->open && params);                                            \
    } while (0)
int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) {
    CHECK_PCM_PARAMS();
    return configure_result();
}
int snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int access) {
    CHECK_PCM_PARAMS();
    CHECK(access == SND_PCM_ACCESS_RW_INTERLEAVED);
    return configure_result();
}
int snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int format) {
    CHECK_PCM_PARAMS();
    CHECK(format == SND_PCM_FORMAT_S16_LE);
    return configure_result();
}
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned channels) {
    CHECK_PCM_PARAMS();
    CHECK(channels == 1);
    return configure_result();
}
int snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned *rate,
                                    int *direction) {
    CHECK_PCM_PARAMS();
    CHECK(*rate == 16000 && !direction);
    *rate = (unsigned)negotiated_rate;
    return configure_result();
}
int snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                           snd_pcm_uframes_t *frames, int *direction) {
    CHECK_PCM_PARAMS();
    CHECK(*frames > 0 && !direction);
    return configure_result();
}
int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                           snd_pcm_uframes_t *frames) {
    CHECK_PCM_PARAMS();
    CHECK(*frames > 0);
    return configure_result();
}
int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params) {
    CHECK_PCM_PARAMS();
    return configure_result();
}
int snd_pcm_get_params(snd_pcm_t *pcm, snd_pcm_uframes_t *buffer, snd_pcm_uframes_t *period) {
    CHECK(pcm == &hardware && pcm->open);
    *buffer = 960;
    *period = 160;
    return configure_result();
}
int snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params) {
    CHECK_PCM_PARAMS();
    params->threshold = 1;
    return configure_result();
}
int snd_pcm_sw_params_set_avail_min(snd_pcm_t *pcm, snd_pcm_sw_params_t *params,
                                    snd_pcm_uframes_t frames) {
    CHECK_PCM_PARAMS();
    CHECK(frames > 0);
    return configure_result();
}
int snd_pcm_sw_params_set_start_threshold(snd_pcm_t *pcm, snd_pcm_sw_params_t *params,
                                          snd_pcm_uframes_t frames) {
    CHECK_PCM_PARAMS();
    params->threshold = frames;
    return configure_result();
}
int snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params) {
    CHECK_PCM_PARAMS();
    return configure_result();
}
int snd_pcm_sw_params_get_start_threshold(const snd_pcm_sw_params_t *params,
                                          snd_pcm_uframes_t *frames) {
    CHECK(params);
    *frames = params->threshold;
    return configure_result();
}
int snd_pcm_prepare(snd_pcm_t *pcm) {
    CHECK(pcm == &hardware && pcm->open);
    ++prepare_calls;
    return prepare_error;
}
int snd_pcm_drop(snd_pcm_t *pcm) {
    CHECK(pcm == &hardware && pcm->open);
    return drop_error;
}
int snd_pcm_drain(snd_pcm_t *pcm) {
    CHECK(pcm == &hardware && pcm->open);
    ++drain_calls;
    if (drain_again) {
        --drain_again;
        return -EAGAIN;
    }
    return drain_error;
}
snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t frames) {
    CHECK(pcm == &hardware && pcm->open);
    ++read_calls;
    snd_pcm_sframes_t result = read_index < read_count ? read_script[read_index++] : -EAGAIN;
    CHECK(result <= (snd_pcm_sframes_t)frames);
    for (snd_pcm_sframes_t i = 0; i < result; ++i)
        ((int16_t *)buffer)[i] = 42;
    return result;
}
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t frames) {
    CHECK(pcm == &hardware && pcm->open);
    ++write_calls;
    snd_pcm_sframes_t result = write_index < write_count ? write_script[write_index++] : -EAGAIN;
    CHECK(result <= (snd_pcm_sframes_t)frames);
    if (result > 0) {
        CHECK(written_count + (size_t)result <=
              sizeof(written_samples) / sizeof(written_samples[0]));
        memcpy(written_samples + written_count, buffer, (size_t)result * sizeof(int16_t));
        written_count += (size_t)result;
    }
    return result;
}
int snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *frames) {
    CHECK(pcm == &hardware && pcm->open);
    *frames = 0;
    return 0;
}
int snd_pcm_state(snd_pcm_t *pcm) {
    CHECK(pcm == &hardware && pcm->open);
    return 0;
}
const char *snd_pcm_state_name(int state) {
    (void)state;
    return "mock";
}

static void check_released(void) {
    CHECK(allocations == 0 && mutexes == 0 && handles == 0);
}
static void test_partial_initialization(void) {
    const mybot_audio_capture_ops_t *ops = rv1126b_capture_ops();
    void *ctx = NULL;
    CHECK(ops->init(&ctx, 16000, 1, 16) == 0);
    int steps = init_step;
    ops->destroy(ctx);
    check_released();
    /* Exercise every external ALSA acquisition/configuration failure, including
     * failure after open has transferred ownership of its handle. */
    for (int step = 1; step <= steps; ++step) {
        init_step = 0;
        fail_step = step;
        ctx = NULL;
        CHECK(ops->init(&ctx, 16000, 1, 16) == -1 && ctx == NULL);
        check_released();
    }
    fail_step = 0;
    fail_alloc = 1;
    CHECK(ops->init(&ctx, 16000, 1, 16) == -1);
    check_released();
    fail_alloc = 0;
    /* Playback owns a separate gain buffer. Failure of its second allocation
     * must release the already acquired context and mutex without opening PCM. */
    for (int allocation = 1; allocation <= 2; ++allocation) {
        fail_alloc = allocation;
        ctx = NULL;
        CHECK(rv1126b_playback_ops()->init(&ctx, 16000, 1, 16) == -1 && ctx == NULL);
        CHECK(fail_alloc == 0);
        check_released();
    }
    fail_mutex = 1;
    CHECK(ops->init(&ctx, 16000, 1, 16) == -1);
    check_released();
    fail_mutex = 0;
    negotiated_rate = 48000;
    CHECK(ops->init(&ctx, 16000, 1, 16) == -1);
    check_released();
    negotiated_rate = 16000;
    CHECK(ops->init(&ctx, 48000, 1, 16) == -1);
    CHECK(ops->init(&ctx, 16000, 2, 16) == -1);
    CHECK(ops->init(&ctx, 16000, 1, 8) == -1);
    check_released();
}

static void test_capture_deadline_and_stop(void) {
    const mybot_audio_capture_ops_t *ops = rv1126b_capture_ops();
    void *ctx = NULL;
    int16_t samples[320] = {0};
    CHECK(ops->init(&ctx, 16000, 1, 16) == 0);
    prepare_error = -EIO;
    CHECK(ops->start(ctx) == -1);
    unsigned calls = read_calls;
    CHECK(ops->read(ctx, samples, 320) == 0 && calls == read_calls);
    prepare_error = 0;
    CHECK(ops->start(ctx) == 0);
    read_count = 1;
    read_index = 0;
    read_script[0] = 100;
    uint64_t started = fake_ms;
    CHECK(ops->read(ctx, samples, 320) == 0);
    CHECK(fake_ms - started >= 20 && fake_ms - started <= 25);
    CHECK(samples[0] == 0); /* Partial hardware periods are never returned as SDK frames. */
    CHECK(ops->stop(ctx) == 0 && ops->start(ctx) == 0);
    read_count = 1;
    read_index = 0;
    read_script[0] = 320;
    CHECK(ops->read(ctx, samples, 320) == 320); /* Old partial frame was discarded. */
    CHECK(samples[0] == 42 && samples[319] == 42);

    read_count = read_index = 0;
    stop_during_wait = ctx;
    calls = read_calls;
    CHECK(ops->read(ctx, samples, 320) == 0);
    CHECK(stop_during_wait == NULL && read_calls == calls + 1);
    CHECK(ops->read(ctx, samples, 320) == 0 && read_calls == calls + 1);
    CHECK(ops->start(ctx) == 0);
    drop_error = -EIO;
    CHECK(ops->stop(ctx) == -1);
    CHECK(ops->read(ctx, samples, 320) == 0 && read_calls == calls + 1);
    drop_error = 0;
    ops->destroy(ctx);
    check_released();
}

static void test_playback_recovery_and_deadline(void) {
    const mybot_audio_playback_ops_t *ops = rv1126b_playback_ops();
    const mybot_audio_volume_ops_t *volume = rv1126b_volume_ops();
    char storage[] = "/tmp/mybot-audio-volume-test-XXXXXX";
    CHECK(mkdtemp(storage));
    CHECK(rv1126b_volume_prepare(storage) == 0);
    void *ctx = NULL;
    void *volume_ctx = NULL;
    int16_t samples[320], original[320], half[320];
    const int16_t source[] = {-32768, -32767, -101, -100, -1, 0, 1, 100, 101, 32767};
    const int16_t scaled[] = {-16384, -16384, -51, -50, -1, 0, 1, 50, 51, 16384};
    for (size_t i = 0; i < 320; ++i) {
        samples[i] = source[i % 10];
        half[i] = scaled[i % 10];
    }
    memcpy(original, samples, sizeof(samples));
    /* Match SDK startup: playback opens before the volume adapter attaches. */
    CHECK(ops->init(&ctx, 16000, 1, 16) == 0);
    CHECK(volume->init(&volume_ctx) == 0);
    CHECK(volume->set_volume(volume_ctx, 50) == 0);
    CHECK(ops->start(ctx) == 0);
    unsigned prepares = prepare_calls;
    write_count = 3;
    write_index = written_count = 0;
    write_script[0] = 100;
    write_script[1] = -EPIPE;
    write_script[2] = 220;
    CHECK(ops->write(ctx, samples, 320) == 320);
    CHECK(prepare_calls == prepares + 1 && written_count == 320);
    CHECK(memcmp(written_samples, half, sizeof(half)) == 0);
    CHECK(memcmp(samples, original, sizeof(samples)) == 0);
    uint64_t started = fake_ms;
    CHECK(ops->write(ctx, samples, 320) == 0);
    CHECK(fake_ms - started >= 20 && fake_ms - started <= 25);

    /* A deadline may return an accepted prefix. Retrying the original suffix
     * at a new gain must neither resend that prefix nor attenuate it twice. */
    write_count = 1;
    write_index = written_count = 0;
    write_script[0] = 100;
    started = fake_ms;
    CHECK(ops->write(ctx, samples, 320) == 100);
    CHECK(fake_ms - started >= 20 && fake_ms - started <= 25);
    CHECK(written_count == 100);
    CHECK(memcmp(written_samples, half, 100 * sizeof(*half)) == 0);
    CHECK(memcmp(samples, original, sizeof(samples)) == 0);
    CHECK(volume->set_volume(volume_ctx, 100) == 0);
    write_count = 1;
    write_index = 0;
    write_script[0] = 220;
    CHECK(ops->write(ctx, samples + 100, 220) == 220);
    CHECK(written_count == 320);
    CHECK(memcmp(written_samples + 100, original + 100, 220 * sizeof(*original)) == 0);
    CHECK(memcmp(samples, original, sizeof(samples)) == 0);

    /* Muting changes the actual PCM boundary while retaining borrowed input. */
    CHECK(volume->set_volume(volume_ctx, 0) == 0);
    write_count = 1;
    write_index = written_count = 0;
    write_script[0] = 320;
    CHECK(ops->write(ctx, samples, 320) == 320);
    CHECK(written_count == 320);
    for (size_t i = 0; i < written_count; ++i)
        CHECK(written_samples[i] == 0);
    CHECK(memcmp(samples, original, sizeof(samples)) == 0);

    CHECK(volume->set_volume(volume_ctx, 50) == 0);
    CHECK(ops->stop(ctx) == 0);
    unsigned calls = write_calls;
    CHECK(ops->write(ctx, samples, 320) == 0 && write_calls == calls);
    ops->destroy(ctx);
    volume->destroy(volume_ctx);
    check_released();

    /* A Wi-Fi transition recreates SDK/PCM owners, retaining product volume. */
    ctx = volume_ctx = NULL;
    CHECK(ops->init(&ctx, 16000, 1, 16) == 0);
    CHECK(volume->init(&volume_ctx) == 0);
    int restored = -1;
    CHECK(volume->get_volume(volume_ctx, &restored) == 0 && restored == 50);
    CHECK(ops->start(ctx) == 0);
    write_count = 1;
    write_index = written_count = 0;
    write_script[0] = 320;
    CHECK(ops->write(ctx, samples, 320) == 320);
    CHECK(written_count == 320 && !memcmp(written_samples, half, sizeof(half)));
    CHECK(!memcmp(samples, original, sizeof(samples)));
    CHECK(ops->stop(ctx) == 0);
    ops->destroy(ctx);
    volume->destroy(volume_ctx);
    check_released();
    rv1126b_volume_shutdown();
    char record[512];
    CHECK(snprintf(record, sizeof(record), "%s/volume.settings", storage) > 0);
    CHECK(unlink(record) == 0);
    CHECK(rmdir(storage) == 0);
}

static void test_playback_drain(void) {
    const mybot_audio_playback_ops_t *ops = rv1126b_playback_ops();
    void *ctx = NULL;
    CHECK(ops->init(&ctx, 16000, 1, 16) == 0 && ops->start(ctx) == 0);
    CHECK(rv1126b_playback_drain(NULL, 100) < 0);
    CHECK(rv1126b_playback_drain(ctx, 0) < 0);
    CHECK(rv1126b_playback_drain(ctx, 1001) < 0);
    drain_again = 2;
    unsigned calls = drain_calls;
    CHECK(rv1126b_playback_drain(ctx, 100) == 0 && drain_calls == calls + 3);
    int16_t samples[320] = {0};
    calls = write_calls;
    CHECK(ops->write(ctx, samples, 320) == 0 && calls == write_calls);
    CHECK(ops->start(ctx) == 0);
    drain_again = 1000;
    uint64_t started = fake_ms;
    CHECK(rv1126b_playback_drain(ctx, 100) < 0);
    CHECK(fake_ms - started >= 100 && fake_ms - started <= 105);
    drain_again = 0;
    drain_error = -EIO;
    CHECK(rv1126b_playback_drain(ctx, 100) < 0);
    drain_error = 0;
    stop_during_wait = ctx;
    drain_again = 1;
    CHECK(rv1126b_playback_drain(ctx, 100) < 0 && stop_during_wait == NULL);
    CHECK(ops->stop(ctx) == 0);
    ops->destroy(ctx);
    check_released();
}

int main(void) {
    CHECK(MYBOT_AUDIO_PTIME_MS == 20);
    test_partial_initialization();
    test_capture_deadline_and_stop();
    test_playback_recovery_and_deadline();
    test_playback_drain();
    puts("ALSA ownership, bounded I/O, recovery and persistent output gain: PASS");
    return 0;
}
