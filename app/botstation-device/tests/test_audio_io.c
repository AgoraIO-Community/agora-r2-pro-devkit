/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_platform.h"

#include <alsa/asoundlib.h>
#include <mybot/mybot_build_config.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static int s_read_calls;
static int s_write_calls;
static int s_read_mode;
static int s_write_mode;
static int s_sample;
static int s_written;

snd_pcm_sframes_t __wrap_snd_pcm_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t frames) {
    (void)pcm;
    ++s_read_calls;
    if (s_read_mode == 1 || (s_read_mode == 0 && s_read_calls == 2) ||
        (s_read_mode == 3 && s_read_calls > 1)) {
        return -EAGAIN;
    }
    if (s_read_mode == 2 && s_read_calls == 1) {
        return -EPIPE;
    }
    snd_pcm_uframes_t count = frames > 80 ? 80 : frames;
    int16_t *samples = buffer;
    for (snd_pcm_uframes_t i = 0; i < count; ++i) {
        samples[i] = (int16_t)s_sample++;
    }
    return (snd_pcm_sframes_t)count;
}

snd_pcm_sframes_t __wrap_snd_pcm_writei(snd_pcm_t *pcm, const void *buffer,
                                      snd_pcm_uframes_t frames) {
    (void)pcm;
    ++s_write_calls;
    if (s_write_mode == 1 || (s_write_mode == 0 && s_write_calls == 2) ||
        (s_write_mode == 3 && s_write_calls > 1)) {
        return -EAGAIN;
    }
    if ((s_write_mode == 2 && s_write_calls == 1) || s_write_mode == 4 ||
        (s_write_mode == 5 && s_write_calls == 2)) {
        return -EPIPE;
    }
    snd_pcm_uframes_t count = frames > 80 ? 80 : frames;
    const int16_t *samples = buffer;
    for (snd_pcm_uframes_t i = 0; i < count; ++i) {
        CHECK(samples[i] == s_written++);
    }
    return (snd_pcm_sframes_t)count;
}

static uint64_t now_ms(void) {
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

int main(void) {
    const rv1126b_platform_config_t config = {
        .capture_device = "null", .playback_device = "null", .network_interface = "",
        .input_device = "", .talk_key = 164,
    };
    CHECK(rv1126b_platform_register(&config) == 0);
    const mybot_audio_capture_ops_t *capture = rv1126b_capture_ops();
    const mybot_audio_playback_ops_t *playback = rv1126b_playback_ops();
    void *ctx = NULL;
    int16_t samples[320] = {0};
    CHECK(capture->init(&ctx, 16000, 1, 16) == 0);
    CHECK(capture->start(ctx) == 0);
    CHECK(capture->read(ctx, samples, 320) == 320);
    CHECK(s_read_calls >= 5);
    for (int i = 0; i < 320; ++i) {
        CHECK(samples[i] == i);
    }
    s_read_mode = 1;
    uint64_t start = now_ms();
    CHECK(capture->read(ctx, samples, 320) == 0);
    CHECK(now_ms() - start < MYBOT_AUDIO_PTIME_MS + 500);
    s_read_mode = 3;
    s_read_calls = 0;
    CHECK(capture->read(ctx, samples, 320) == 0);
    CHECK(s_sample == 400);
    s_read_mode = 0;
    CHECK(capture->read(ctx, samples, 320) == 320);
    for (int i = 0; i < 320; ++i) {
        CHECK(samples[i] == i + 320);
    }
    s_read_mode = 2;
    s_read_calls = 0;
    CHECK(capture->read(ctx, samples, 320) == 0);
    CHECK(capture->read(ctx, samples, 320) == 320);
    CHECK(capture->stop(ctx) == 0);
    int calls = s_read_calls;
    CHECK(capture->read(ctx, samples, 320) == 0);
    CHECK(s_read_calls == calls);
    capture->destroy(ctx);

    for (int i = 0; i < 320; ++i) {
        samples[i] = (int16_t)i;
    }
    CHECK(playback->init(&ctx, 16000, 1, 16) == 0);
    CHECK(playback->start(ctx) == 0);
    CHECK(playback->write(ctx, samples, 320) == 320);
    CHECK(s_written == 320 && s_write_calls >= 5);
    s_write_mode = 1;
    start = now_ms();
    CHECK(playback->write(ctx, samples, 320) == 0);
    CHECK(now_ms() - start < MYBOT_AUDIO_PTIME_MS + 500);
    s_write_mode = 3;
    s_write_calls = 0;
    s_written = 0;
    CHECK(playback->write(ctx, samples, 320) == 80);
    s_write_mode = 0;
    CHECK(playback->write(ctx, samples + 80, 240) == 240);
    CHECK(s_written == 320);
    s_write_mode = 2;
    s_write_calls = 0;
    s_written = 0;
    CHECK(playback->write(ctx, samples, 320) == 320);
    CHECK(s_written == 320);
    s_write_mode = 5;
    s_write_calls = 0;
    s_written = 0;
    CHECK(playback->write(ctx, samples, 320) == 320);
    CHECK(s_written == 320);
    s_write_mode = 4;
    s_write_calls = 0;
    start = now_ms();
    CHECK(playback->write(ctx, samples, 320) == 0);
    CHECK(s_write_calls == 2);
    CHECK(now_ms() - start < MYBOT_AUDIO_PTIME_MS + 500);
    CHECK(playback->stop(ctx) == 0);
    calls = s_write_calls;
    CHECK(playback->write(ctx, samples, 320) == 0);
    CHECK(s_write_calls == calls);
    playback->destroy(ctx);
    puts("BotStation short audio I/O tests passed");
    return 0;
}
