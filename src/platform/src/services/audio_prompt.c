/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/prompt.h"
#include "mybot_platform/audio.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PROMPT_FRAME_SAMPLES 320
#define PROMPT_MAX_BYTES (1024 * 1024)
#define PROMPT_STALL_MS 250
#define PROMPT_FRAME_TIMEOUT_MS 1000
#define PROMPT_DRAIN_MS 100

static pthread_mutex_t prompt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t prompt_thread;
static bool prompt_joinable;
static bool prompt_cancelled;
static bool prompt_running;
static char prompt_assets[PATH_MAX];
static char prompt_locale[6];

static bool cancelled(void) {
    return __atomic_load_n(&prompt_cancelled, __ATOMIC_ACQUIRE);
}

static int monotonic_ms(uint64_t *value) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return -1;
    }
    *value = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return 0;
}

static int open_prompt(size_t *bytes) {
    int root = open(prompt_assets, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) {
        return -1;
    }
    int locales = openat(root, "locales", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    close(root);
    if (locales < 0) {
        return -1;
    }
    int language = openat(locales, prompt_locale, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    close(locales);
    if (language < 0) {
        return -1;
    }
    int fd = openat(language, "wificonfig.pcm", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    close(language);
    if (fd < 0) {
        return -1;
    }
    struct stat status;
    if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        status.st_size > PROMPT_MAX_BYTES || status.st_size % 2 != 0) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    *bytes = (size_t)status.st_size;
    return fd;
}

static int read_frame(int fd, unsigned char *bytes, size_t count) {
    size_t used = 0;
    while (used < count && !cancelled()) {
        ssize_t result = read(fd, bytes + used, count - used);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return -1;
        }
        used += (size_t)result;
    }
    return cancelled() ? -1 : 0;
}

static int write_frame(const mybot_audio_playback_ops_t *ops, void *context, const int16_t *samples,
                       int count) {
    int accepted = 0;
    uint64_t started, last_progress;
    if (monotonic_ms(&started) < 0) {
        return -1;
    }
    last_progress = started;
    while (accepted < count && !cancelled()) {
        int written = ops->write(context, samples + accepted, count - accepted);
        if (written < 0 || written > count - accepted) {
            return -1;
        }
        accepted += written;
        if (accepted == count) {
            return 0;
        }
        uint64_t now;
        if (monotonic_ms(&now) < 0) {
            return -1;
        }
        if (written > 0) {
            last_progress = now;
        }
        if (now - last_progress >= PROMPT_STALL_MS || now - started >= PROMPT_FRAME_TIMEOUT_MS) {
            return -1;
        }
        if (written == 0) {
            const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return -1;
}

static void *prompt_worker(void *unused) {
    (void)unused;
    const mybot_audio_playback_ops_t *ops = rv1126b_playback_ops();
    void *context = NULL;
    bool started = false;
    size_t remaining = 0;
    int fd = open_prompt(&remaining);
    if (fd < 0) {
        fprintf(stderr, "Wi-Fi prompt: cannot read a valid locale PCM asset\n");
        goto cleanup;
    }
    if (cancelled()) {
        goto cleanup;
    }
    if (!ops || !ops->init || !ops->start || !ops->write || !ops->stop || !ops->destroy ||
        ops->init(&context, 16000, 1, 16) < 0) {
        fprintf(stderr, "Wi-Fi prompt: cannot open playback\n");
        goto cleanup;
    }
    if (cancelled()) {
        goto cleanup;
    }
    if (ops->start(context) < 0) {
        fprintf(stderr, "Wi-Fi prompt: cannot start playback\n");
        goto cleanup;
    }
    started = true;
    while (remaining > 0 && !cancelled()) {
        unsigned char bytes[PROMPT_FRAME_SAMPLES * 2];
        int16_t samples[PROMPT_FRAME_SAMPLES];
        size_t count = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
        if (read_frame(fd, bytes, count) < 0) {
            if (!cancelled()) {
                fprintf(stderr, "Wi-Fi prompt: cannot read PCM frame\n");
            }
            goto cleanup;
        }
        for (size_t i = 0; i < count / 2; ++i) {
            int sample = bytes[i * 2] | (int)bytes[i * 2 + 1] << 8;
            samples[i] = (int16_t)(sample >= 32768 ? sample - 65536 : sample);
        }
        /* The shared ALSA boundary applies the saved output gain exactly once. */
        if (write_frame(ops, context, samples, (int)(count / 2)) < 0) {
            if (!cancelled()) {
                fprintf(stderr, "Wi-Fi prompt: playback failed or stopped making progress\n");
            }
            goto cleanup;
        }
        remaining -= count;
    }
    if (!cancelled() && remaining == 0 && rv1126b_playback_drain(context, PROMPT_DRAIN_MS) < 0) {
        fprintf(stderr, "Wi-Fi prompt: playback did not drain before its deadline\n");
    }
cleanup:
    if (context) {
        if (started) {
            ops->stop(context);
        }
        ops->destroy(context);
    }
    if (fd >= 0) {
        close(fd);
    }
    __atomic_store_n(&prompt_running, false, __ATOMIC_RELEASE);
    return NULL;
}

int rv1126b_prompt_start(const char *assets_directory, const char *locale) {
    if (!assets_directory || !*assets_directory || !locale ||
        (strcmp(locale, "zh-CN") && strcmp(locale, "en-US")) ||
        strlen(assets_directory) >= sizeof(prompt_assets)) {
        errno = EINVAL;
        return -1;
    }
    int result = pthread_mutex_trylock(&prompt_lock);
    if (result) {
        errno = result;
        return -1;
    }
    if (prompt_joinable) {
        pthread_mutex_unlock(&prompt_lock);
        errno = EBUSY;
        return -1;
    }
    strcpy(prompt_assets, assets_directory);
    strcpy(prompt_locale, locale);
    __atomic_store_n(&prompt_cancelled, false, __ATOMIC_RELEASE);
    __atomic_store_n(&prompt_running, true, __ATOMIC_RELEASE);
    result = pthread_create(&prompt_thread, NULL, prompt_worker, NULL);
    if (result == 0) {
        prompt_joinable = true;
    } else {
        __atomic_store_n(&prompt_running, false, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&prompt_lock);
    if (result) {
        errno = result;
        return -1;
    }
    return 0;
}

void rv1126b_prompt_stop(void) {
    pthread_mutex_lock(&prompt_lock);
    if (prompt_joinable) {
        __atomic_store_n(&prompt_cancelled, true, __ATOMIC_RELEASE);
        pthread_join(prompt_thread, NULL);
        prompt_joinable = false;
    }
    pthread_mutex_unlock(&prompt_lock);
}

bool rv1126b_prompt_active(void) {
    return __atomic_load_n(&prompt_running, __ATOMIC_ACQUIRE);
}
