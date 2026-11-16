/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/time_sync.h"
#include "time_sync_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TIME_MINIMUM_EPOCH INT64_C(1767225600)

struct rv1126b_time_sync {
    rv1126b_time_sync_backend_t backend;
    void *backend_context;
    unsigned int timeout_seconds;
    char helper[PATH_MAX];
    char error[192];
    uint64_t online_started;
    bool online_window;
    bool helper_attempted;
    bool helper_cancelled;
    pid_t helper_pid;
    rv1126b_time_sync_state_t state;
};

static int system_realtime(void *unused, int64_t *seconds) {
    (void)unused;
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0) {
        return -1;
    }
    *seconds = (int64_t)now.tv_sec;
    return 0;
}

static int system_monotonic(void *unused, uint64_t *milliseconds) {
    (void)unused;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return -1;
    }
    *milliseconds = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return 0;
}

static bool numeric_pid(const char *name) {
    if (!*name) {
        return false;
    }
    for (; *name; ++name) {
        if (*name < '0' || *name > '9') {
            return false;
        }
    }
    return true;
}

static bool time_service_name(const char *name) {
    /* Linux comm truncates names to 15 characters. */
    return !strcmp(name, "ntpd") || !strcmp(name, "chronyd") || !strcmp(name, "openntpd") ||
           !strcmp(name, "systemd-timesyn") || !strcmp(name, "systemd-timesyncd");
}

bool rv1126b_time_sync_process_is_service(const char *comm, const char *cmdline, size_t length) {
    if (comm && time_service_name(comm)) {
        return true;
    }
    if (!cmdline || !length) {
        return false;
    }
    const char *end = memchr(cmdline, '\0', length);
    if (!end || end == cmdline) {
        return false;
    }
    const char *basename = strrchr(cmdline, '/');
    basename = basename ? basename + 1 : cmdline;
    if (time_service_name(basename)) {
        return true;
    }
    if (strcmp(basename, "busybox") && (!comm || strcmp(comm, "busybox"))) {
        return false;
    }
    size_t offset = (size_t)(end - cmdline) + 1;
    if (offset >= length || !memchr(cmdline + offset, '\0', length - offset)) {
        return false;
    }
    return time_service_name(cmdline + offset);
}

static int system_service_running(void *unused) {
    (void)unused;
    DIR *directory = opendir("/proc");
    if (!directory) {
        return -1;
    }
    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) {
                result = -1;
            }
            break;
        }
        if (!numeric_pid(entry->d_name)) {
            continue;
        }
        char path[64];
        int length = snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
        if (length < 0 || length >= (int)sizeof(path)) {
            result = -1;
            break;
        }
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            if (errno == ENOENT || errno == ESRCH) {
                continue;
            }
            result = -1; /* An unreadable service inventory cannot authorize a helper. */
            break;
        }
        char name[64];
        ssize_t count;
        do {
            count = read(fd, name, sizeof(name) - 1);
        } while (count < 0 && errno == EINTR);
        int saved_errno = errno;
        close(fd);
        if (count < 0) {
            if (saved_errno == ENOENT || saved_errno == ESRCH) {
                continue;
            }
            result = -1;
            break;
        }
        name[count] = '\0';
        if (count > 0 && name[count - 1] == '\n') {
            name[count - 1] = '\0';
        }
        if (rv1126b_time_sync_process_is_service(name, NULL, 0)) {
            result = 1;
            break;
        }
        if (!strcmp(name, "busybox")) {
            length = snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
            if (length < 0 || length >= (int)sizeof(path)) {
                result = -1;
                break;
            }
            fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) {
                if (errno == ENOENT || errno == ESRCH) {
                    continue;
                }
                result = -1;
                break;
            }
            char command[PATH_MAX * 2];
            do {
                count = read(fd, command, sizeof(command));
            } while (count < 0 && errno == EINTR);
            saved_errno = errno;
            close(fd);
            if (count < 0) {
                if (saved_errno == ENOENT || saved_errno == ESRCH) {
                    continue;
                }
                result = -1;
                break;
            }
            if (rv1126b_time_sync_process_is_service(name, command, (size_t)count)) {
                result = 1;
                break;
            }
        }
    }
    closedir(directory);
    return result;
}

extern char **environ;

static int system_spawn_helper(void *unused, const char *path, pid_t *pid) {
    (void)unused;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    struct stat status;
    bool valid = fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
                 !(status.st_mode & (S_ISUID | S_ISGID));
    close(fd);
    if (!valid || access(path, X_OK) < 0) {
        errno = EACCES;
        return -1;
    }
    posix_spawnattr_t attributes;
    int result = posix_spawnattr_init(&attributes);
    if (result) {
        errno = result;
        return -1;
    }
    posix_spawn_file_actions_t actions;
    result = posix_spawn_file_actions_init(&actions);
    if (result) {
        posix_spawnattr_destroy(&attributes);
        errno = result;
        return -1;
    }
    result = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!result) {
        result = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (!result) {
        result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    }
    char *const arguments[] = {(char *)path, NULL};
    if (!result) {
        result = posix_spawn(pid, path, &actions, &attributes, arguments, environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (result) {
        errno = result;
        return -1;
    }
    return 0;
}

static int system_helper_exited(void *unused, pid_t pid) {
    (void)unused;
    if (pid <= 1) {
        errno = EINVAL;
        return -1;
    }
    siginfo_t information;
    memset(&information, 0, sizeof(information));
    int result;
    do {
        result = waitid(P_PID, (id_t)pid, &information, WEXITED | WNOHANG | WNOWAIT);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        return -1;
    }
    return information.si_pid == pid ? 1 : 0;
}

static int system_cancel_helper(void *unused, pid_t pid) {
    (void)unused;
    /* Verify this is still our unreaped child before addressing its group. A
     * retained zombie reserves the PID until group cancellation and waitpid. */
    if (system_helper_exited(NULL, pid) < 0) {
        return -1;
    }
    if (kill(-pid, SIGKILL) < 0 && errno != ESRCH) {
        return -1;
    }
    if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
        return -1;
    }
    return 0;
}

static int system_reap_helper(void *unused, pid_t pid, bool block) {
    (void)unused;
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, block ? 0 : WNOHANG);
    } while (result < 0 && errno == EINTR);
    return result == pid ? 1 : result == 0 ? 0 : -1;
}

const rv1126b_time_sync_backend_t *rv1126b_time_sync_default_backend(void) {
    static const rv1126b_time_sync_backend_t backend = {
        .realtime = system_realtime,
        .monotonic = system_monotonic,
        .service_running = system_service_running,
        .spawn_helper = system_spawn_helper,
        .helper_exited = system_helper_exited,
        .cancel_helper = system_cancel_helper,
        .reap_helper = system_reap_helper,
    };
    return &backend;
}

rv1126b_time_sync_t *
rv1126b_time_sync_create_with_backend(const rv1126b_time_sync_config_t *config,
                                      const rv1126b_time_sync_backend_t *backend, void *context) {
    unsigned int timeout = config ? config->timeout_seconds : 30;
    const char *helper = config && config->helper_path ? config->helper_path : "";
    if (timeout < 1 || timeout > 300 || (*helper && helper[0] != '/') ||
        strlen(helper) >= PATH_MAX || !backend || !backend->realtime || !backend->monotonic ||
        !backend->service_running || !backend->spawn_helper || !backend->helper_exited ||
        !backend->cancel_helper || !backend->reap_helper) {
        errno = EINVAL;
        return NULL;
    }
    rv1126b_time_sync_t *sync = calloc(1, sizeof(*sync));
    if (!sync) {
        return NULL;
    }
    sync->backend = *backend;
    sync->backend_context = context;
    sync->timeout_seconds = timeout;
    strcpy(sync->helper, helper);
    sync->state = RV1126B_TIME_WAIT_NETWORK;
    return sync;
}

rv1126b_time_sync_t *rv1126b_time_sync_create(const rv1126b_time_sync_config_t *config) {
    return rv1126b_time_sync_create_with_backend(config, rv1126b_time_sync_default_backend(), NULL);
}

/* Returns 1 once all owned child/group resources have been cancelled and reaped,
 * or 0 while SIGKILL is pending. It never targets a system service. */
static int release_helper(rv1126b_time_sync_t *sync, bool block) {
    if (!sync->helper_pid) {
        return 1;
    }
    if (!sync->helper_cancelled) {
        if (sync->backend.cancel_helper(sync->backend_context, sync->helper_pid) < 0) {
            if (errno == ECHILD) {
                sync->helper_pid = 0; /* Never signal a PID already reaped elsewhere. */
            }
            return -1;
        }
        sync->helper_cancelled = true;
    }
    int result = sync->backend.reap_helper(sync->backend_context, sync->helper_pid, block);
    if (result == 1 || (result < 0 && errno == ECHILD)) {
        sync->helper_pid = 0;
        sync->helper_cancelled = false;
    }
    return result;
}

static rv1126b_time_sync_state_t fail(rv1126b_time_sync_t *sync, const char *message) {
    snprintf(sync->error, sizeof(sync->error), "%s", message);
    sync->state = RV1126B_TIME_FAILED;
    return sync->state;
}

rv1126b_time_sync_state_t rv1126b_time_sync_poll(rv1126b_time_sync_t *sync, bool network_ready) {
    if (!sync) {
        return RV1126B_TIME_FAILED;
    }
    if (!network_ready) {
        int released = release_helper(sync, false);
        sync->online_window = false;
        sync->helper_attempted = false;
        if (released < 0) {
            return fail(sync, "Cannot cancel the owned time helper; inspect its wrapper");
        }
        sync->error[0] = '\0';
        sync->state = RV1126B_TIME_WAIT_NETWORK;
        return sync->state;
    }
    int64_t realtime;
    if (sync->backend.realtime(sync->backend_context, &realtime) < 0) {
        release_helper(sync, false);
        return fail(sync, "Cannot read system time; inspect the system clock service");
    }
    if (realtime >= TIME_MINIMUM_EPOCH) {
        int released = release_helper(sync, false);
        if (released < 0) {
            return fail(sync, "Cannot reap the owned time helper; inspect its wrapper");
        }
        if (released == 0) {
            sync->state = RV1126B_TIME_WAIT_CLOCK;
            return sync->state;
        }
        sync->error[0] = '\0';
        sync->online_window = false;
        sync->helper_attempted = false;
        sync->state = RV1126B_TIME_READY;
        return sync->state;
    }
    if (sync->helper_cancelled) {
        int released = release_helper(sync, false);
        if (released < 0) {
            return fail(sync, "Cannot reap the owned time helper; inspect its wrapper");
        }
        if (released == 0) {
            sync->state = RV1126B_TIME_WAIT_CLOCK;
            return sync->state;
        }
    }
    if (sync->state == RV1126B_TIME_FAILED || sync->state == RV1126B_TIME_TIMEOUT) {
        return sync->state;
    }
    uint64_t now;
    if (sync->backend.monotonic(sync->backend_context, &now) < 0) {
        release_helper(sync, false);
        return fail(sync, "Cannot read the monotonic clock; inspect system clock support");
    }
    if (!sync->online_window) {
        sync->online_started = now;
        sync->online_window = true;
    }
    if (now < sync->online_started) {
        release_helper(sync, false);
        return fail(sync, "Monotonic time moved backwards; inspect system clock support");
    }
    if (now - sync->online_started >= (uint64_t)sync->timeout_seconds * 1000) {
        int released = release_helper(sync, false);
        if (released < 0) {
            return fail(sync, "Cannot reap the timed-out helper; inspect its wrapper");
        }
        if (released == 0) {
            sync->state = RV1126B_TIME_WAIT_CLOCK;
            return sync->state;
        }
        snprintf(sync->error, sizeof(sync->error),
                 "System time is still invalid; check NTP/network or configure a one-shot helper");
        sync->state = RV1126B_TIME_TIMEOUT;
        return sync->state;
    }
    if (sync->helper_pid) {
        int exited = sync->backend.helper_exited(sync->backend_context, sync->helper_pid);
        if (exited < 0) {
            release_helper(sync, false);
            return fail(sync, "Cannot inspect the owned helper; inspect its process lifecycle");
        }
        if (exited > 0) {
            release_helper(sync, false);
            return fail(sync,
                        "Time helper exited without a valid clock; check its NTP configuration");
        }
    } else if (*sync->helper && !sync->helper_attempted) {
        sync->helper_attempted = true;
        int running = sync->backend.service_running(sync->backend_context);
        if (running < 0) {
            return fail(
                sync, "Cannot inspect /proc time services; check access before enabling a helper");
        }
        if (running == 0) {
            pid_t pid = 0;
            if (sync->backend.spawn_helper(sync->backend_context, sync->helper, &pid) < 0 ||
                pid <= 1) {
                return fail(
                    sync, "Cannot start time helper; use an absolute executable one-shot wrapper");
            }
            sync->helper_pid = pid;
        }
    }
    sync->state = RV1126B_TIME_WAIT_CLOCK;
    return sync->state;
}

const char *rv1126b_time_sync_error(const rv1126b_time_sync_t *sync) {
    return sync ? sync->error : "Time readiness service is unavailable";
}

void rv1126b_time_sync_destroy(rv1126b_time_sync_t *sync) {
    if (!sync) {
        return;
    }
    if (release_helper(sync, true) < 0) {
        fprintf(stderr, "Time readiness: cannot reap the owned helper\n");
    }
    free(sync);
}
