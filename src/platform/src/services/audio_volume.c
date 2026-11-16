/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/volume.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VOLUME_FILE "volume.settings"
#define VOLUME_DEFAULT 100

static pthread_mutex_t volume_lock = PTHREAD_MUTEX_INITIALIZER;
static int storage_fd = -1;
static int volume_level = VOLUME_DEFAULT;
static bool volume_persisted;
static bool volume_attached;
static unsigned long temporary_sequence;

static bool private_directory(int fd) {
    struct stat status;
    return fstat(fd, &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == geteuid() &&
           (status.st_mode & 07777) == 0700;
}

static bool private_record(const struct stat *status) {
    return S_ISREG(status->st_mode) && status->st_uid == geteuid() &&
           (status->st_mode & 07777) == 0600 && status->st_nlink == 1;
}

/* -1 means an unsafe/unreadable file; 0 means missing/corrupt; 1 means valid. */
static int load_level(int directory, int *level) {
    int fd = openat(directory, VOLUME_FILE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    struct stat status;
    if (fstat(fd, &status) < 0 || !private_record(&status)) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    char text[8];
    size_t used = 0;
    int result = 0;
    while (used < sizeof(text)) {
        ssize_t count = read(fd, text + used, sizeof(text) - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            result = -1;
            break;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    close(fd);
    if (result < 0) {
        return -1;
    }
    bool valid = used >= 2 && used <= 4 && text[used - 1] == '\n';
    int parsed = 0;
    for (size_t i = 0; valid && i + 1 < used; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            valid = false;
        } else {
            parsed = parsed * 10 + text[i] - '0';
        }
    }
    valid = valid && parsed <= MYBOT_AUDIO_VOLUME_MAX && (used == 2 || text[0] != '0');
    if (!valid) {
        fprintf(stderr, "Volume: invalid saved level; using 100 until a new level is saved\n");
        return 0;
    }
    *level = parsed;
    return 1;
}

/* volume_lock held. Only replace our regular private file, never a link. */
static int save_level(int level) {
    if (!private_directory(storage_fd)) {
        errno = EACCES;
        return -1;
    }
    struct stat status;
    if (fstatat(storage_fd, VOLUME_FILE, &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!private_record(&status)) {
            errno = EACCES;
            return -1;
        }
    } else if (errno != ENOENT) {
        return -1;
    }
    char temporary[80];
    int fd = -1;
    for (unsigned int attempt = 0; attempt < 32; ++attempt) {
        snprintf(temporary, sizeof(temporary), ".volume.settings.%ld.%lu", (long)getpid(),
                 ++temporary_sequence);
        fd = openat(storage_fd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    0600);
        if (fd >= 0 || errno != EEXIST) {
            break;
        }
    }
    if (fd < 0) {
        return -1;
    }
    char text[8];
    int length = snprintf(text, sizeof(text), "%d\n", level);
    size_t written = 0;
    bool ok = fchmod(fd, 0600) == 0;
    while (ok && written < (size_t)length) {
        ssize_t count = write(fd, text + written, (size_t)length - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            ok = false;
        } else {
            written += (size_t)count;
        }
    }
    if (ok) {
        ok = fsync(fd) == 0;
    }
    if (close(fd) < 0) {
        ok = false;
    }
    if (ok) {
        ok = renameat(storage_fd, temporary, storage_fd, VOLUME_FILE) == 0;
    }
    if (ok) {
        ok = fsync(storage_fd) == 0;
    }
    if (!ok) {
        int saved_errno = errno;
        unlinkat(storage_fd, temporary, 0);
        /* A directory-sync failure may follow rename. Force the next set to
         * rewrite the record even when restoring the last published level. */
        volume_persisted = false;
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int rv1126b_volume_prepare(const char *private_storage_directory) {
    if (!private_storage_directory || !*private_storage_directory) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&volume_lock);
    if (storage_fd >= 0) {
        pthread_mutex_unlock(&volume_lock);
        errno = EBUSY;
        return -1;
    }
    int fd = -1;
    if (mkdir(private_storage_directory, 0700) == 0 || errno == EEXIST) {
        fd = open(private_storage_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    int level = VOLUME_DEFAULT;
    int loaded = -1;
    if (fd >= 0 && private_directory(fd)) {
        loaded = load_level(fd, &level);
    }
    if (loaded < 0) {
        if (fd >= 0) {
            close(fd);
        }
        pthread_mutex_unlock(&volume_lock);
        fprintf(stderr, "Volume: cannot open private saved-level storage\n");
        return -1;
    }
    storage_fd = fd;
    __atomic_store_n(&volume_level, level, __ATOMIC_RELEASE);
    volume_persisted = loaded == 1;
    volume_attached = false;
    pthread_mutex_unlock(&volume_lock);
    return 0;
}

static int volume_init(void **context) {
    if (!context) {
        return -1;
    }
    *context = NULL;
    pthread_mutex_lock(&volume_lock);
    int result = -1;
    if (storage_fd >= 0 && !volume_attached) {
        volume_attached = true;
        *context = &volume_level;
        result = 0;
    }
    pthread_mutex_unlock(&volume_lock);
    return result;
}

static int set_volume(void *context, int level) {
    if (level < MYBOT_AUDIO_VOLUME_MIN || level > MYBOT_AUDIO_VOLUME_MAX) {
        return -1;
    }
    pthread_mutex_lock(&volume_lock);
    int result = -1;
    if (context == &volume_level && storage_fd >= 0 && volume_attached) {
        if (volume_persisted && level == __atomic_load_n(&volume_level, __ATOMIC_RELAXED)) {
            result = 0;
        } else if (save_level(level) == 0) {
            __atomic_store_n(&volume_level, level, __ATOMIC_RELEASE);
            volume_persisted = true;
            result = 0;
        } else {
            fprintf(stderr, "Volume: cannot save level; keeping the current level\n");
        }
    }
    pthread_mutex_unlock(&volume_lock);
    return result;
}

static int get_volume(void *context, int *level) {
    if (!level) {
        return -1;
    }
    pthread_mutex_lock(&volume_lock);
    int result = -1;
    if (context == &volume_level && storage_fd >= 0 && volume_attached) {
        *level = __atomic_load_n(&volume_level, __ATOMIC_ACQUIRE);
        result = 0;
    }
    pthread_mutex_unlock(&volume_lock);
    return result;
}

static void volume_destroy(void *context) {
    pthread_mutex_lock(&volume_lock);
    if (context == &volume_level) {
        volume_attached = false;
    }
    pthread_mutex_unlock(&volume_lock);
}

const mybot_audio_volume_ops_t *rv1126b_volume_ops(void) {
    static const mybot_audio_volume_ops_t operations = {
        .init = volume_init,
        .set_volume = set_volume,
        .get_volume = get_volume,
        .destroy = volume_destroy,
    };
    return &operations;
}

int rv1126b_volume_level(void) {
    return __atomic_load_n(&volume_level, __ATOMIC_ACQUIRE);
}

void rv1126b_volume_scale(int16_t *output, const int16_t *input, size_t samples) {
    if (!output || !input) {
        return;
    }
    const int level = rv1126b_volume_level();
    for (size_t i = 0; i < samples; ++i) {
        int32_t scaled = (int32_t)input[i] * level;
        scaled = scaled < 0 ? (scaled - 50) / 100 : (scaled + 50) / 100;
        output[i] = (int16_t)scaled;
    }
}

void rv1126b_volume_shutdown(void) {
    pthread_mutex_lock(&volume_lock);
    if (storage_fd >= 0) {
        close(storage_fd);
    }
    storage_fd = -1;
    __atomic_store_n(&volume_level, VOLUME_DEFAULT, __ATOMIC_RELEASE);
    volume_attached = false;
    volume_persisted = false;
    pthread_mutex_unlock(&volume_lock);
}
