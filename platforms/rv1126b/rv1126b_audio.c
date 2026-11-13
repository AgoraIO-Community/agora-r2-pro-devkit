/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_platform.h"

#include <alsa/asoundlib.h>
#include <alloca.h>
#include <api/aosl_log.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mybot/mybot_build_config.h>

typedef struct {
    snd_pcm_t *pcm;
    pthread_mutex_t lock;
    bool running;
    int16_t *pending;
    size_t capacity;
    size_t pending_frames;
    const char *direction;
    bool capture;
    uint64_t last_write_ms;
    uint64_t report_ms;
    uint64_t written_frames;
    uint64_t max_write_gap_ms;
    unsigned int playback_recoveries;
    int playback_peak;
} pcm_context_t;

static uint64_t tick_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        AOSL_LOG_ERR("audio monotonic clock: %s", strerror(errno));
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int configure_period(pcm_context_t *ctx) {
    snd_pcm_hw_params_t *hw;
    snd_pcm_sw_params_t *sw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_uframes_t period = 16000 * MYBOT_AUDIO_PTIME_MS / 2000;
    snd_pcm_uframes_t buffer = ctx->capture ? 1920 : 16000 * 60 / 1000;
    snd_pcm_uframes_t threshold = 1;
    unsigned int rate = 16000;
    int err = snd_pcm_hw_params_any(ctx->pcm, hw);
    if (err >= 0) {
        err = snd_pcm_hw_params_set_access(ctx->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    }
    if (err >= 0) {
        err = snd_pcm_hw_params_set_format(ctx->pcm, hw, SND_PCM_FORMAT_S16_LE);
    }
    if (err >= 0) {
        err = snd_pcm_hw_params_set_channels(ctx->pcm, hw, 1);
    }
    if (err >= 0) {
        err = snd_pcm_hw_params_set_rate_near(ctx->pcm, hw, &rate, NULL);
        if (err >= 0 && rate != 16000) {
            err = -EINVAL;
        }
    }
    if (err >= 0) {
        err = snd_pcm_hw_params_set_period_size_near(ctx->pcm, hw, &period, NULL);
    }
    if (err >= 0) {
        err = snd_pcm_hw_params_set_buffer_size_near(ctx->pcm, hw, &buffer);
    }
    if (err >= 0) {
        err = snd_pcm_hw_params(ctx->pcm, hw);
    }
    if (err >= 0) {
        err = snd_pcm_get_params(ctx->pcm, &buffer, &period);
    }
    if (err >= 0) {
        err = snd_pcm_sw_params_current(ctx->pcm, sw);
    }
    if (err >= 0) {
        err = snd_pcm_sw_params_set_avail_min(ctx->pcm, sw, period);
    }
    if (err >= 0) {
        threshold = ctx->capture ? 1 : 16000 * MYBOT_AUDIO_PTIME_MS * 2 / 1000;
        if (threshold > buffer) {
            threshold = buffer;
        }
        err = snd_pcm_sw_params_set_start_threshold(ctx->pcm, sw, threshold);
    }
    if (err >= 0) {
        err = snd_pcm_sw_params(ctx->pcm, sw);
    }
    if (err >= 0) {
        err = snd_pcm_sw_params_current(ctx->pcm, sw);
    }
    if (err >= 0) {
        err = snd_pcm_sw_params_get_start_threshold(sw, &threshold);
    }
    if (err >= 0) {
        AOSL_LOG_NTC("ALSA %s: rate=%u Hz format=S16_LE channels=1 "
                     "period=%lu buffer=%lu frames, SDK frame=%d frames cloud_aec=%d "
                     "period_ms=%lu buffer_ms=%lu start_threshold_ms=%lu",
                     ctx->direction, rate, (unsigned long)period, (unsigned long)buffer,
                     16000 * MYBOT_AUDIO_PTIME_MS / 1000, MYBOT_CLOUD_AEC,
                     (unsigned long)(period * 1000 / rate), (unsigned long)(buffer * 1000 / rate),
                     (unsigned long)(threshold * 1000 / rate));
        if (!ctx->capture && (buffer < rate * 50 / 1000 || buffer > rate * 100 / 1000)) {
            AOSL_LOG_WRN("ALSA playback: negotiated buffer outside 50-100 ms "
                         "(requested 60 ms); verify playback stability and latency");
        }
    }
    return err;
}

static int pcm_init(void **out, int rate, int channels, int bits, snd_pcm_stream_t stream) {
    if (!out || rate != 16000 || channels != 1 || bits != 16) {
        AOSL_LOG_ERR("audio requires 16000 Hz, mono, signed 16-bit PCM");
        return -1;
    }
    pcm_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        AOSL_LOG_ERR("cannot allocate audio context");
        return -1;
    }
    ctx->direction = stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback";
    ctx->capture = stream == SND_PCM_STREAM_CAPTURE;
    int err = pthread_mutex_init(&ctx->lock, NULL);
    if (err != 0) {
        AOSL_LOG_ERR("audio mutex: %s", strerror(err));
        free(ctx);
        return -1;
    }
    const rv1126b_platform_config_t *config = rv1126b_platform_config();
    const char *device =
        stream == SND_PCM_STREAM_CAPTURE ? config->capture_device : config->playback_device;
    err = snd_pcm_open(&ctx->pcm, device, stream, SND_PCM_NONBLOCK);
    if (err >= 0) {
        /* ALSA plug devices may resample hardware, but the SDK-facing rate stays exact. */
        err = configure_period(ctx);
    }
    if (err < 0) {
        AOSL_LOG_ERR("cannot open/configure ALSA %s: %s", device, snd_strerror(err));
        if (ctx->pcm) {
            snd_pcm_close(ctx->pcm);
        }
        pthread_mutex_destroy(&ctx->lock);
        free(ctx);
        return -1;
    }
    *out = ctx;
    return 0;
}

static int capture_init(void **out, int rate, int channels, int bits) {
    return pcm_init(out, rate, channels, bits, SND_PCM_STREAM_CAPTURE);
}

static int playback_init(void **out, int rate, int channels, int bits) {
    return pcm_init(out, rate, channels, bits, SND_PCM_STREAM_PLAYBACK);
}

static int pcm_start(void *opaque) {
    pcm_context_t *ctx = opaque;
    pthread_mutex_lock(&ctx->lock);
    int err = snd_pcm_prepare(ctx->pcm);
    if (err >= 0) {
        ctx->pending_frames = 0;
        ctx->last_write_ms = 0;
        ctx->report_ms = 0;
        ctx->written_frames = 0;
        ctx->max_write_gap_ms = 0;
        ctx->playback_recoveries = 0;
        ctx->playback_peak = 0;
        ctx->running = true;
    }
    pthread_mutex_unlock(&ctx->lock);
    if (err < 0) {
        AOSL_LOG_ERR("ALSA prepare: %s", snd_strerror(err));
    }
    return err < 0 ? -1 : 0;
}

static int pcm_recover(pcm_context_t *ctx, snd_pcm_sframes_t result) {
    if (result == -EAGAIN || result == 0) {
        return 0;
    }
    if (result == -EPIPE || result == -ESTRPIPE) {
        AOSL_LOG_WRN("ALSA %s stream recovery: %s", ctx->direction, snd_strerror((int)result));
        ctx->pending_frames = 0;
        int err = snd_pcm_prepare(ctx->pcm);
        if (err >= 0) {
            return 0;
        }
        result = err;
    }
    AOSL_LOG_ERR("ALSA %s I/O: %s", ctx->direction, snd_strerror((int)result));
    return -1;
}

static int capture_read(void *opaque, void *buffer, int frames) {
    pcm_context_t *ctx = opaque;
    if (!ctx || !buffer || frames <= 0 || (size_t)frames > SIZE_MAX / sizeof(int16_t)) {
        AOSL_LOG_ERR("invalid capture buffer/frame count");
        return -1;
    }
    uint64_t start = tick_ms();
    if (start == 0) {
        return -1;
    }
    uint64_t deadline = start + MYBOT_AUDIO_PTIME_MS;
    pthread_mutex_lock(&ctx->lock);
    int result = 0;
    if (!ctx->running) {
        goto done;
    }
    if ((size_t)frames > ctx->capacity) {
        int16_t *pending = realloc(ctx->pending, (size_t)frames * sizeof(*pending));
        if (!pending) {
            AOSL_LOG_ERR("cannot allocate capture accumulator");
            result = -1;
            goto done;
        }
        ctx->pending = pending;
        ctx->capacity = (size_t)frames;
    }
    while (ctx->pending_frames < (size_t)frames && ctx->running) {
        snd_pcm_sframes_t got = snd_pcm_readi(ctx->pcm, ctx->pending + ctx->pending_frames,
                                              (size_t)frames - ctx->pending_frames);
        if (got < 0 && got != -EAGAIN) {
            result = pcm_recover(ctx, got);
            goto done;
        }
        if (got > 0) {
            ctx->pending_frames += (size_t)got;
        }
        if (ctx->pending_frames >= (size_t)frames) {
            break;
        }
        uint64_t now = tick_ms();
        if (now == 0) {
            result = -1;
            goto done;
        }
        if (now >= deadline) {
            break;
        }
        if (got <= 0) {
            /* Keep stop responsive while waiting for the next hardware period. */
            const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
            pthread_mutex_unlock(&ctx->lock);
            nanosleep(&pause, NULL);
            pthread_mutex_lock(&ctx->lock);
        }
    }
    if (ctx->pending_frames >= (size_t)frames) {
        memcpy(buffer, ctx->pending, (size_t)frames * sizeof(*ctx->pending));
        ctx->pending_frames -= (size_t)frames;
        memmove(ctx->pending, ctx->pending + frames, ctx->pending_frames * sizeof(*ctx->pending));
        result = frames;
    }
done:
    pthread_mutex_unlock(&ctx->lock);
    return result;
}

static int playback_write(void *opaque, const void *buffer, int frames) {
    pcm_context_t *ctx = opaque;
    if (!ctx || !buffer || frames <= 0) {
        AOSL_LOG_ERR("invalid playback buffer/frame count");
        return -1;
    }
    pthread_mutex_lock(&ctx->lock);
    int result = 0;
    uint64_t start = tick_ms();
    if (start == 0) {
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    uint64_t deadline = start + MYBOT_AUDIO_PTIME_MS;
    if (ctx->running && ctx->last_write_ms != 0) {
        uint64_t gap = start - ctx->last_write_ms;
        if (gap > ctx->max_write_gap_ms) {
            ctx->max_write_gap_ms = gap;
        }
    }
    if (ctx->running) {
        ctx->last_write_ms = start;
    }
    bool retried = false;
    while (ctx->running && result < frames) {
        snd_pcm_sframes_t written = snd_pcm_writei(ctx->pcm, (const int16_t *)buffer + result,
                                                   (snd_pcm_uframes_t)(frames - result));
        if (written < 0 && written != -EAGAIN) {
            if (written == -EPIPE || written == -ESTRPIPE) {
                ++ctx->playback_recoveries;
                AOSL_LOG_WRN("[AUDIO] playback recovery=%u accepted=%llu frames "
                             "max_write_gap=%llu ms",
                             ctx->playback_recoveries, (unsigned long long)ctx->written_frames,
                             (unsigned long long)ctx->max_write_gap_ms);
            }
            int recovered = pcm_recover(ctx, written);
            uint64_t now = tick_ms();
            /* Retry the unaccepted suffix without losing another SDK timer period. */
            if (recovered == 0 && !retried && now != 0 && now < deadline) {
                retried = true;
                continue;
            }
            if (result == 0) {
                result = recovered;
            }
            break;
        }
        if (written > 0) {
            const int16_t *samples = (const int16_t *)buffer + result;
            for (snd_pcm_sframes_t i = 0; i < written; ++i) {
                int magnitude = samples[i] < 0 ? -(int)samples[i] : (int)samples[i];
                if (magnitude > ctx->playback_peak) {
                    ctx->playback_peak = magnitude;
                }
            }
            ctx->written_frames += (uint64_t)written;
            result += (int)written;
        }
        if (result == frames) {
            break;
        }
        uint64_t now = tick_ms();
        if (now == 0 || now >= deadline) {
            if (now == 0 && result == 0) {
                result = -1;
            }
            break;
        }
        if (written <= 0) {
            const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
            pthread_mutex_unlock(&ctx->lock);
            nanosleep(&pause, NULL);
            pthread_mutex_lock(&ctx->lock);
        }
    }
    if (ctx->running && (ctx->report_ms == 0 || start - ctx->report_ms >= 5000)) {
        snd_pcm_sframes_t delay = 0;
        int delay_result = snd_pcm_delay(ctx->pcm, &delay);
        if (delay_result < 0) {
            AOSL_LOG_WRN("[AUDIO] cannot query playback delay: %s", snd_strerror(delay_result));
        }
        AOSL_LOG_NTC("[AUDIO] playback accepted=%llu frames peak=%d "
                     "max_write_gap=%llu ms recoveries=%u state=%s alsa_delay_ms=%lld",
                     (unsigned long long)ctx->written_frames, ctx->playback_peak,
                     (unsigned long long)ctx->max_write_gap_ms, ctx->playback_recoveries,
                     snd_pcm_state_name(snd_pcm_state(ctx->pcm)),
                     delay_result < 0 ? -1LL : (long long)delay * 1000 / 16000);
        ctx->report_ms = start;
        ctx->written_frames = 0;
        ctx->max_write_gap_ms = 0;
        ctx->playback_recoveries = 0;
        ctx->playback_peak = 0;
    }
    pthread_mutex_unlock(&ctx->lock);
    return result;
}

static int pcm_stop(void *opaque) {
    pcm_context_t *ctx = opaque;
    pthread_mutex_lock(&ctx->lock);
    ctx->running = false;
    ctx->pending_frames = 0;
    /* I/O is nonblocking and serialized with drop; recovery cannot restart after stop. */
    int err = snd_pcm_drop(ctx->pcm);
    pthread_mutex_unlock(&ctx->lock);
    if (err < 0) {
        AOSL_LOG_ERR("ALSA drop: %s", snd_strerror(err));
    }
    return err < 0 ? -1 : 0;
}

static void pcm_destroy(void *opaque) {
    pcm_context_t *ctx = opaque;
    if (!ctx) {
        return;
    }
    int err = snd_pcm_close(ctx->pcm);
    if (err < 0) {
        AOSL_LOG_ERR("ALSA close: %s", snd_strerror(err));
    }
    pthread_mutex_destroy(&ctx->lock);
    free(ctx->pending);
    free(ctx);
}

const mybot_audio_capture_ops_t *rv1126b_capture_ops(void) {
    static const mybot_audio_capture_ops_t ops = {
        .init = capture_init,
        .start = pcm_start,
        .read = capture_read,
        .stop = pcm_stop,
        .destroy = pcm_destroy,
    };
    return &ops;
}

const mybot_audio_playback_ops_t *rv1126b_playback_ops(void) {
    static const mybot_audio_playback_ops_t ops = {
        .init = playback_init,
        .start = pcm_start,
        .write = playback_write,
        .stop = pcm_stop,
        .destroy = pcm_destroy,
    };
    return &ops;
}
