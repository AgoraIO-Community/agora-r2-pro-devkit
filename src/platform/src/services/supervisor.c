/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/supervisor.h"
#include "wifi/factory_reset_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#define OWNED_CHILDREN_MAX 256
#define LOG_FILES 4

typedef enum { TRANSITION_NONE, TRANSITION_POWEROFF, TRANSITION_RESET } transition_t;

typedef struct {
    rv1126b_supervisor_config_t policy;
    int runtime, logs, lock, listener, output, log, stop_client;
    size_t log_bytes;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *state;
    pid_t child;
    bool stopping, cleaning, failed, unclean;
    transition_t transition;
    int child_result;
    uint64_t terminate_at, cleanup_at, launch_at, restarts[3];
    unsigned int restart_count;
} supervisor_t;

static volatile sig_atomic_t signal_stop;

static void stop_signal(int number) {
    (void)number;
    signal_stop = 1;
}

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int descriptor_flags(int fd, bool nonblocking) {
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        return -1;
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) < 0)
        return -1;
    return 0;
}

static int private_directory(const char *path) {
    if (!path || path[0] != '/')
        return -1;
    if (mkdir(path, 0700) < 0 && errno != EEXIST)
        return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat status;
    if (fd < 0)
        return -1;
    if (fstat(fd, &status) < 0 || !S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
        (status.st_mode & 07777) != 0700) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}

static bool safe_file(int directory, const char *name, bool absent) {
    struct stat status;
    if (fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW) < 0)
        return absent && errno == ENOENT;
    return S_ISREG(status.st_mode) && status.st_uid == geteuid() &&
           (status.st_mode & 07777) == 0600 && status.st_nlink == 1;
}

static int open_log(supervisor_t *service) {
    if (!safe_file(service->logs, "mybot.log", true))
        return -1;
    service->log = openat(service->logs, "mybot.log",
                          O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat status;
    if (service->log < 0 || fstat(service->log, &status) < 0 || status.st_size < 0)
        return -1;
    service->log_bytes = (size_t)status.st_size;
    return 0;
}

static int rotate_log(supervisor_t *service) {
    if (service->log >= 0)
        close(service->log);
    service->log = -1;
    for (int index = LOG_FILES - 1; index >= 1; --index) {
        char from[32], to[32];
        if (index == 1)
            snprintf(from, sizeof(from), "mybot.log");
        else
            snprintf(from, sizeof(from), "mybot.log.%d", index - 1);
        snprintf(to, sizeof(to), "mybot.log.%d", index);
        if (!safe_file(service->logs, from, true) || !safe_file(service->logs, to, true))
            return -1;
        if (renameat(service->logs, from, service->logs, to) < 0 && errno != ENOENT)
            return -1;
    }
    return open_log(service);
}

static int append_log(supervisor_t *service, const char *text, size_t length) {
    while (length) {
        if (service->log_bytes >= service->policy.log_limit_bytes && rotate_log(service) < 0)
            return -1;
        size_t count = service->policy.log_limit_bytes - service->log_bytes;
        if (count > length)
            count = length;
        ssize_t written = write(service->log, text, count);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        text += written;
        length -= (size_t)written;
        service->log_bytes += (size_t)written;
    }
    return 0;
}

static void publish(supervisor_t *service, const char *state) {
    if (!strcmp(service->state, state))
        return;
    service->state = state;
    /* This diagnostic contains no arguments, environment values or device IDs. */
    char text[80];
    int length = snprintf(text, sizeof(text), "supervisor: %s\n", state);
    (void)append_log(service, text, (size_t)length);
}

static void mark_unclean(supervisor_t *service) {
    service->unclean = service->failed = true;
    const char message[] = "Unverified application cleanup; reboot before restarting.\n";
    int fd = openat(service->runtime, "unclean",
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        (void)write(fd, message, sizeof(message) - 1);
        (void)fsync(fd);
        close(fd);
        (void)fsync(service->runtime);
    }
}

static int drain_log(supervisor_t *service) {
    if (service->output < 0)
        return 0;
    char buffer[4096];
    for (unsigned int chunks = 0; chunks < 16; ++chunks) {
        ssize_t count = read(service->output, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        if (count < 0)
            return -1;
        if (count == 0) {
            close(service->output);
            service->output = -1;
            return 0;
        }
        if (append_log(service, buffer, (size_t)count) < 0)
            return -1;
    }
    return 0;
}

/* An unreaped direct child reserves its PID. Never signal a PID from a pidfile. */
static int observe(pid_t pid, siginfo_t *information) {
    memset(information, 0, sizeof(*information));
    int result;
    do {
        result = waitid(P_PID, (id_t)pid, information, WEXITED | WNOHANG | WNOWAIT);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int signal_owned(pid_t pid, int number, bool group) {
    siginfo_t information;
    if (pid <= 1 || observe(pid, &information) < 0)
        return -1;
    if (group && getpgid(pid) == pid && kill(-pid, number) < 0 && errno != ESRCH)
        return -1;
    if (!information.si_pid && kill(pid, number) < 0 && errno != ESRCH)
        return -1;
    return 0;
}

#ifdef __linux__
static int status_owned_children(pid_t *children, size_t *count) {
    DIR *directory = opendir("/proc");
    if (!directory)
        return -1;
    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno)
                result = -1;
            break;
        }
        char *end;
        errno = 0;
        long pid = strtol(entry->d_name, &end, 10);
        if (errno || *end || pid <= 1 || pid > INT_MAX)
            continue;
        char path[64];
        int length = snprintf(path, sizeof(path), "/proc/%ld/status", pid);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            result = -1;
            break;
        }
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) {
            if (errno == ENOENT || errno == ESRCH)
                continue;
            result = -1;
            break;
        }
        char text[4096];
        ssize_t size;
        do {
            size = read(fd, text, sizeof(text) - 1);
        } while (size < 0 && errno == EINTR);
        int saved_errno = errno;
        close(fd);
        if (size < 0) {
            if (saved_errno == ENOENT || saved_errno == ESRCH)
                continue;
            result = -1;
            break;
        }
        text[size] = '\0';
        const char *parent_field = strstr(text, "\nPPid:");
        long parent;
        if (!parent_field || sscanf(parent_field, "\nPPid: %ld", &parent) != 1 ||
            parent != (long)getpid())
            continue;
        siginfo_t information;
        if (observe((pid_t)pid, &information) < 0) {
            if (errno == ECHILD)
                continue;
            result = -1;
            break;
        }
        if (*count == OWNED_CHILDREN_MAX) {
            result = -1;
            break;
        }
        children[(*count)++] = (pid_t)pid;
    }
    closedir(directory);
    return result;
}
#endif

static int owned_children(supervisor_t *service, pid_t *children, size_t *count) {
    *count = 0;
#ifdef __linux__
    (void)service;
    char path[80], text[OWNED_CHILDREN_MAX * 12 + 1];
    snprintf(path, sizeof(path), "/proc/self/task/%ld/children", (long)getpid());
#ifdef RV1126B_SUPERVISOR_FORCE_STATUS_SCAN
    return status_owned_children(children, count);
#endif
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && (errno == ENOENT || errno == ENOTDIR))
        return status_owned_children(children, count);
    if (fd < 0)
        return -1;
    size_t used = 0;
    while (used + 1 < sizeof(text)) {
        ssize_t size = read(fd, text + used, sizeof(text) - used - 1);
        if (size < 0 && errno == EINTR)
            continue;
        if (size < 0) {
            close(fd);
            return -1;
        }
        if (!size)
            break;
        used += (size_t)size;
    }
    close(fd);
    if (used + 1 == sizeof(text))
        return -1;
    text[used] = '\0';
    char *cursor = text;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\n')
            ++cursor;
        if (!*cursor)
            break;
        errno = 0;
        char *end;
        long pid = strtol(cursor, &end, 10);
        if (errno || end == cursor || pid <= 1 || pid > INT_MAX ||
            (*end && *end != ' ' && *end != '\n') || *count == OWNED_CHILDREN_MAX)
            return -1;
        children[(*count)++] = (pid_t)pid;
        cursor = end;
    }
#else
    /* Host tests use the direct child's dedicated process group. Production
     * is Linux and requires subreaper adoption for independently grouped heirs. */
    if (service->child > 1)
        children[(*count)++] = service->child;
#endif
    return 0;
}

static int cleanup_children(supervisor_t *service) {
    pid_t children[OWNED_CHILDREN_MAX];
    size_t count;
    if (owned_children(service, children, &count) < 0)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        pid_t pid = children[i];
        int number =
            service->transition != TRANSITION_NONE && !service->unclean ? SIGTERM : SIGKILL;
        if (signal_owned(pid, number, true) < 0 && errno != ECHILD)
            return -1;
        int status;
        pid_t reaped;
        do {
            reaped = waitpid(pid, &status, WNOHANG);
        } while (reaped < 0 && errno == EINTR);
        if (reaped == pid && service->child == pid)
            service->child = 0;
        if (reaped < 0 && errno != ECHILD)
            return -1;
    }
    /* Re-enumerate after reaping: newly orphaned grandchildren become direct
     * children of this subreaper, including helpers in different process groups. */
    if (owned_children(service, children, &count) < 0)
        return -1;
    return count == 0 ? 1 : 0;
}

static int launch(supervisor_t *service) {
    int output[2];
    if (pipe(output) < 0)
        return -1;
    if (descriptor_flags(output[0], true) < 0 || descriptor_flags(output[1], false) < 0) {
        close(output[0]);
        close(output[1]);
        return -1;
    }
    pid_t parent = getpid();
    pid_t child = fork();
    if (child == 0) {
        /* The supervisor is single-threaded. This path uses async-signal-safe
         * syscalls only before exec; no stdio, allocation or environment edits. */
        if (setpgid(0, 0) < 0)
            _exit(126);
#ifdef __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) < 0 || getppid() != parent)
            _exit(126);
#else
        (void)parent;
#endif
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGTERM, &action, NULL) < 0 || sigaction(SIGINT, &action, NULL) < 0)
            _exit(126);
        int input = open("/dev/null", O_RDONLY);
        if (input < 0 || dup2(input, STDIN_FILENO) < 0 || dup2(output[1], STDOUT_FILENO) < 0 ||
            dup2(output[1], STDERR_FILENO) < 0)
            _exit(126);
        if (input > STDERR_FILENO)
            close(input);
        close(output[0]);
        if (output[1] > STDERR_FILENO)
            close(output[1]);
        execv(service->policy.argv[0], service->policy.argv);
        _exit(127);
    }
    close(output[1]);
    if (child < 0) {
        close(output[0]);
        return -1;
    }
    service->child = child;
    service->output = output[0];
    service->cleaning = false;
    service->child_result = 1;
    publish(service, "running");
    return 0;
}

static void reply(int fd, const char *message) {
    if (fd >= 0) {
        (void)write(fd, message, strlen(message));
        close(fd);
    }
}

static void begin_stop(supervisor_t *service, uint64_t now) {
    if (service->stopping)
        return;
    service->stopping = true;
    /* A concurrent stop takes priority over relaunch. The accepted marker
     * survives for the next boot, and the child is already releasing resources. */
    if (service->transition == TRANSITION_RESET)
        return;
    service->terminate_at = now + service->policy.terminate_timeout_ms;
    if (service->child > 1 && signal_owned(service->child, SIGTERM, false) < 0 && errno != ECHILD)
        service->failed = true;
    publish(service, "stopping");
}

static bool poweroff_command_safe(const char *path) {
    if (!path || path[0] != '/')
        return false;
    char resolved[PATH_MAX];
    struct stat status;
    if (!realpath(path, resolved) || stat(resolved, &status) < 0 || !S_ISREG(status.st_mode) ||
        !(status.st_mode & S_IXUSR) || (status.st_mode & 0022))
        return false;
#ifdef RV1126B_SUPERVISOR_TEST_ALLOW_UNPRIVILEGED
    if (status.st_uid != geteuid())
        return false;
#else
    if (geteuid() != 0 || status.st_uid != 0)
        return false;
    /* Every resolved ancestor must protect the verified executable from
     * replacement. /sbin/poweroff may legitimately resolve to BusyBox. */
    for (char *separator = resolved + 1; *separator; ++separator) {
        if (*separator != '/')
            continue;
        *separator = '\0';
        bool safe = stat(resolved, &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == 0 &&
                    !(status.st_mode & 0022);
        *separator = '/';
        if (!safe)
            return false;
    }
#endif
    return true;
}

static bool peer_is_application(supervisor_t *service, int fd) {
    pid_t peer = -1;
    uid_t owner = (uid_t)-1;
#ifdef __linux__
    struct ucred credentials;
    socklen_t size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0 ||
        size != sizeof(credentials))
        return false;
    peer = credentials.pid;
    owner = credentials.uid;
#elif defined(__APPLE__)
    gid_t group;
    socklen_t size = sizeof(peer);
    if (getpeereid(fd, &owner, &group) < 0 ||
        getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &peer, &size) < 0 || size != sizeof(peer))
        return false;
#else
    (void)fd;
    return false;
#endif
#ifdef RV1126B_SUPERVISOR_TEST_ALLOW_UNPRIVILEGED
    if (owner != geteuid())
        return false;
#else
    if (owner != 0 || geteuid() != 0)
        return false;
#endif
    siginfo_t information;
    return peer > 1 && peer == service->child && observe(peer, &information) == 0 &&
           !information.si_pid;
}

/* An empty, exclusively created file is a complete journal record. Its name
 * and the empty contents become durable before the final acknowledgement. */
static bool persist_reset(const char *path) {
    int directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0)
        return false;
    struct stat status;
    bool valid = fstat(directory, &status) == 0 && status.st_uid == geteuid() &&
                 (status.st_mode & 07777) == 0700;
    const char *name = "factory-reset.pending";
    int fd =
        valid ? openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)
              : -1;
    bool saved = false;
    if (fd >= 0) {
        saved = fsync(fd) == 0;
        if (close(fd) < 0 || fsync(directory) < 0)
            saved = false;
        if (!saved && unlinkat(directory, name, 0) == 0)
            (void)fsync(directory);
    }
    close(directory);
    return saved;
}

static void accept_transition(supervisor_t *service, int fd, transition_t transition) {
    if (service->failed || service->unclean || service->stopping || service->cleaning ||
        service->transition != TRANSITION_NONE || !peer_is_application(service, fd) ||
        (transition == TRANSITION_RESET &&
         (!service->policy.reset_directory ||
          !factory_station_supported(getenv("RV1126B_WIFI_INTERFACE")))) ||
        (transition == TRANSITION_POWEROFF &&
         !poweroff_command_safe(service->policy.poweroff_command))) {
        reply(fd, "failed\n");
        return;
    }
    /* Commit only if the application received its acknowledgement. A missing
     * ACK keeps the application and supervisor alive without arming shutdown. */
    if (write(fd, "accepted\n", 9) != 9) {
        close(fd);
        return;
    }
    struct timeval commit_timeout = {.tv_sec = 1, .tv_usec = 500000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &commit_timeout, sizeof(commit_timeout)) < 0) {
        close(fd);
        return;
    }
    char commit[8] = {0};
    size_t used = 0;
    while (used < 7) {
        ssize_t count = read(fd, commit + used, 7 - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        used += (size_t)count;
        if (memchr(commit, '\n', used))
            break;
    }
    uint64_t now = milliseconds();
    if (used != 7 || memcmp(commit, "commit\n", 7) || !now) {
        close(fd);
        return;
    }
    if (transition == TRANSITION_RESET && !persist_reset(service->policy.reset_directory)) {
        reply(fd, "failed\n");
        return;
    }
    bool acknowledged = write(fd, "committed\n", 10) == 10;
    close(fd);
    if (!acknowledged && transition == TRANSITION_POWEROFF)
        return;
    service->transition = transition;
    service->stopping = transition == TRANSITION_POWEROFF;
    service->terminate_at = now + service->policy.terminate_timeout_ms;
    /* A committed reset survives loss of its final acknowledgement. Ask the
     * direct child to exit cleanly even when it cannot observe the response. */
    if (!acknowledged && signal_owned(service->child, SIGTERM, false) < 0)
        mark_unclean(service);
    /* The application owns orderly resource release; do not signal it while
     * its acknowledgement is still returning to the product thread. */
    publish(service, transition == TRANSITION_RESET ? "factory_reset_pending" : "poweroff_pending");
}

static void accept_control(supervisor_t *service, uint64_t now) {
    int fd = accept(service->listener, NULL, NULL);
    if (fd < 0)
        return;
    (void)descriptor_flags(fd, false);
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char request[16] = {0};
    ssize_t count = 0;
    while ((size_t)count < sizeof(request) - 1) {
        ssize_t size = read(fd, request + count, sizeof(request) - 1 - (size_t)count);
        if (size < 0 && errno == EINTR)
            continue;
        if (size <= 0)
            break;
        count += size;
        if (memchr(request, '\n', (size_t)count))
            break;
    }
    if (count == 7 && !memcmp(request, "status\n", 7)) {
        char response[48];
        snprintf(response, sizeof(response), "%s\n", service->state);
        reply(fd, response);
    } else if (count == 9 && !memcmp(request, "poweroff\n", 9)) {
        accept_transition(service, fd, TRANSITION_POWEROFF);
    } else if (count == 14 && !memcmp(request, "factory_reset\n", 14)) {
        accept_transition(service, fd, TRANSITION_RESET);
    } else if (count == 5 && !memcmp(request, "stop\n", 5) && service->stop_client < 0) {
        service->stop_client = fd;
        begin_stop(service, now);
    } else {
        reply(fd, "failed\n");
    }
}

static int initialize(supervisor_t *service) {
    service->runtime = private_directory(service->policy.runtime_directory);
    service->logs = private_directory(service->policy.log_directory);
    if (service->runtime < 0 || service->logs < 0)
        return -1;
    if (!safe_file(service->runtime, "supervisor.lock", true))
        return -1;
    service->lock = openat(service->runtime, "supervisor.lock",
                           O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (service->lock < 0 || flock(service->lock, LOCK_EX | LOCK_NB) < 0 || open_log(service) < 0)
        return -1;
    struct stat status;
    if (fstatat(service->runtime, "control.sock", &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid() ||
            unlinkat(service->runtime, "control.sock", 0) < 0)
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }
    int length = snprintf(service->socket_path, sizeof(service->socket_path), "%s/control.sock",
                          service->policy.runtime_directory);
    if (length < 0 || (size_t)length >= sizeof(service->socket_path))
        return -1;
    service->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (service->listener < 0 || descriptor_flags(service->listener, true) < 0)
        return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, service->socket_path, (size_t)length + 1);
    if (bind(service->listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        chmod(service->socket_path, 0600) < 0 || listen(service->listener, 8) < 0)
        return -1;
#ifdef __linux__
    if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0)
        return -1;
#endif
    pid_t children[OWNED_CHILDREN_MAX];
    size_t count;
    if (owned_children(service, children, &count) < 0 || count != 0)
        return -1;
    if (fstatat(service->runtime, "unclean", &status, AT_SYMLINK_NOFOLLOW) == 0) {
        service->unclean = service->failed = true;
    } else if (errno != ENOENT) {
        return -1;
    }
    return 0;
}

static void dispose(supervisor_t *service) {
    if (service->stop_client >= 0)
        reply(service->stop_client, service->failed ? "failed\n" : "stopped\n");
    if (service->listener >= 0) {
        close(service->listener);
        if (service->socket_path[0])
            unlink(service->socket_path);
    }
    if (service->output >= 0)
        close(service->output);
    if (service->log >= 0)
        close(service->log);
    if (service->lock >= 0)
        close(service->lock);
    if (service->runtime >= 0)
        close(service->runtime);
    if (service->logs >= 0)
        close(service->logs);
}

int rv1126b_supervisor_run(const rv1126b_supervisor_config_t *config) {
    if (!config || !config->runtime_directory || !config->log_directory || !config->argv ||
        !config->argv[0] || config->argv[0][0] != '/' ||
        (config->poweroff_command && !poweroff_command_safe(config->poweroff_command)) ||
        (config->reset_directory && config->reset_directory[0] != '/'))
        return 1;
    supervisor_t service = {.policy = *config,
                            .runtime = -1,
                            .logs = -1,
                            .lock = -1,
                            .listener = -1,
                            .output = -1,
                            .log = -1,
                            .stop_client = -1,
                            .state = "starting"};
    if (!service.policy.terminate_timeout_ms)
        service.policy.terminate_timeout_ms = 10000;
    if (!service.policy.cleanup_timeout_ms)
        service.policy.cleanup_timeout_ms = 5000;
    if (!service.policy.restart_window_ms)
        service.policy.restart_window_ms = 300000;
    const unsigned int delays[] = {2000, 5000, 15000};
    for (unsigned int i = 0; i < 3; ++i)
        if (!service.policy.restart_delays_ms[i])
            service.policy.restart_delays_ms[i] = delays[i];
    if (!service.policy.log_limit_bytes)
        service.policy.log_limit_bytes = 512 * 1024;
    umask(0077);
    struct sigaction action = {.sa_handler = stop_signal};
    sigemptyset(&action.sa_mask);
    signal_stop = 0;
    if (sigaction(SIGTERM, &action, NULL) < 0 || sigaction(SIGINT, &action, NULL) < 0) {
        return 1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0 || initialize(&service) < 0) {
        fprintf(stderr, "Supervisor: cannot acquire private runtime/log ownership: %s\n",
                strerror(errno));
        dispose(&service);
        return 1;
    }
    if (service.unclean || launch(&service) < 0) {
        service.failed = true;
        publish(&service, "failed");
    }
    bool finished = false;
    while (!finished) {
        uint64_t now = milliseconds();
        if (!now) {
            service.failed = true;
            signal_stop = 1;
        }
        if (signal_stop) {
            signal_stop = 0;
            begin_stop(&service, now);
        }
        struct pollfd descriptors[2] = {{.fd = service.listener, .events = POLLIN},
                                        {.fd = service.output, .events = POLLIN}};
        int result = poll(descriptors, 2, 20);
        if (result < 0 && errno != EINTR) {
            service.failed = true;
            begin_stop(&service, now);
        }
        if (descriptors[0].revents & POLLIN)
            accept_control(&service, now);
        if (drain_log(&service) < 0) {
            service.failed = true;
            begin_stop(&service, now);
        }
        if (service.child > 1 && !service.cleaning) {
            siginfo_t information;
            if (observe(service.child, &information) < 0) {
                service.failed = true;
                service.cleaning = true;
            } else if (information.si_pid == service.child) {
                service.child_result =
                    information.si_code == CLD_EXITED ? information.si_status : 1;
                if (service.child_result == RV1126B_EXIT_UNCLEAN ||
                    (information.si_code != CLD_EXITED &&
                     (!service.stopping || service.transition != TRANSITION_NONE)))
                    mark_unclean(&service);
                if (service.transition != TRANSITION_NONE && service.child_result != 0) {
                    service.failed = true;
                    if (service.transition == TRANSITION_RESET && !service.unclean)
                        mark_unclean(&service);
                }
                service.cleaning = true;
            } else if ((service.stopping || service.transition != TRANSITION_NONE) &&
                       now >= service.terminate_at) {
                mark_unclean(&service);
                service.cleaning = true;
            }
            if (service.cleaning)
                service.cleanup_at = now + service.policy.cleanup_timeout_ms;
        }
        if (service.cleaning || (service.stopping && !service.child)) {
            int clean = cleanup_children(&service);
            if (clean == 1) {
                (void)drain_log(&service);
                if (service.output >= 0)
                    close(service.output);
                service.output = -1;
                service.cleaning = false;
                if (service.unclean) {
                    service.stopping = false;
                    publish(&service, "failed");
                    if (service.stop_client >= 0) {
                        reply(service.stop_client, "failed\n");
                        service.stop_client = -1;
                    }
                } else if (service.transition == TRANSITION_RESET && !service.stopping &&
                           !service.failed) {
                    /* launch() execs the same wrapper as a cold boot. It
                     * completes the journal before launching a fresh app. */
                    service.transition = TRANSITION_NONE;
                    service.launch_at = now;
                } else if (service.stopping || service.transition != TRANSITION_NONE ||
                           service.child_result == 0) {
                    if (service.stopping && service.transition == TRANSITION_NONE)
                        service.failed = false; /* A verified clean stop permits replacement. */
                    publish(&service, service.failed ? "failed" : "stopped");
                    finished = true;
                } else if (!service.failed) {
                    unsigned int kept = 0;
                    for (unsigned int i = 0; i < service.restart_count; ++i)
                        if (now - service.restarts[i] < service.policy.restart_window_ms)
                            service.restarts[kept++] = service.restarts[i];
                    service.restart_count = kept;
                    if (kept == 3) {
                        service.failed = true;
                        publish(&service, "failed");
                    } else {
                        service.restarts[kept] = now;
                        service.launch_at = now + service.policy.restart_delays_ms[kept];
                        ++service.restart_count;
                        publish(&service, "backoff");
                    }
                } else {
                    publish(&service, "failed");
                }
            } else if (clean < 0 || now >= service.cleanup_at) {
                if (service.transition != TRANSITION_NONE)
                    mark_unclean(&service);
                service.failed = true;
                publish(&service, "failed");
                if (service.stop_client >= 0) {
                    reply(service.stop_client, "failed\n");
                    service.stop_client = -1;
                }
                /* Retain ownership and keep retrying cleanup. No new child or
                 * successful stop is permitted until all adopted heirs exit. */
            }
        }
        if (!service.failed && !service.stopping && service.launch_at && now >= service.launch_at) {
            service.launch_at = 0;
            if (launch(&service) < 0) {
                service.failed = true;
                publish(&service, "failed");
            }
        }
    }
    bool poweroff = service.transition == TRANSITION_POWEROFF && !service.failed &&
                    !service.unclean && service.child_result == 0;
    int poweroff_log = -1;
    if (poweroff) {
        if (!poweroff_command_safe(service.policy.poweroff_command)) {
            service.failed = true;
            poweroff = false;
            const char message[] = "Supervisor: unsafe poweroff command\n";
            (void)append_log(&service, message, sizeof(message) - 1);
            publish(&service, "failed");
        } else {
            publish(&service, "poweroff");
            poweroff_log = fcntl(service.log, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
            if (poweroff_log < 0) {
                const char message[] = "Supervisor: cannot preserve poweroff diagnostics\n";
                (void)append_log(&service, message, sizeof(message) - 1);
                service.failed = true;
                poweroff = false;
                publish(&service, "failed");
            }
            if (service.log >= 0)
                (void)fsync(service.log);
        }
    }
    dispose(&service);
    if (poweroff) {
        /* Close the control socket before PID1 can execute rcK/S99mybot stop:
         * shutdown must never wait for a supervisor waiting for shutdown. */
        if (dup2(poweroff_log, STDERR_FILENO) < 0) {
            (void)dprintf(poweroff_log, "Supervisor: cannot attach poweroff diagnostics\n");
            close(poweroff_log);
            return 1;
        }
        close(poweroff_log);
        if (!poweroff_command_safe(service.policy.poweroff_command)) {
            fprintf(stderr, "Supervisor: unsafe poweroff command\n");
            return 1;
        }
        sync();
        char *const arguments[] = {(char *)service.policy.poweroff_command, NULL};
        execv(service.policy.poweroff_command, arguments);
        fprintf(stderr, "Supervisor: cannot execute poweroff helper: %s\n", strerror(errno));
        return 1;
    }
    return service.failed ? 1 : 0;
}

int rv1126b_supervisor_control(const char *runtime_directory, int stop) {
    if (!runtime_directory || runtime_directory[0] != '/')
        return 1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int length =
        snprintf(address.sun_path, sizeof(address.sun_path), "%s/control.sock", runtime_directory);
    if (length < 0 || (size_t)length >= sizeof(address.sun_path))
        return 1;
    char marker[PATH_MAX];
    if (snprintf(marker, sizeof(marker), "%s/unclean", runtime_directory) >= (int)sizeof(marker))
        return 1;
    struct stat unclean_status;
    bool unclean = lstat(marker, &unclean_status) == 0;
    if (unclean) {
        puts("failed");
        return 1;
    }
    struct stat status;
    if (lstat(address.sun_path, &status) < 0) {
        if (errno == ENOENT) {
            puts(unclean ? "failed" : "stopped");
            return unclean ? 1 : 3;
        }
        return 1;
    }
    if (!S_ISSOCK(status.st_mode) || status.st_uid != geteuid() || (status.st_mode & 0777) != 0600)
        return 1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 1;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        if (errno == ENOENT || errno == ECONNREFUSED) {
            puts(unclean ? "failed" : "stopped");
            return unclean ? 1 : 3;
        }
        return 1;
    }
    const char *request = stop ? "stop\n" : "status\n";
    if (write(fd, request, strlen(request)) != (ssize_t)strlen(request)) {
        close(fd);
        return 1;
    }
    struct pollfd reply_fd = {.fd = fd, .events = POLLIN};
    int ready;
    do {
        ready = poll(&reply_fd, 1, stop ? 30000 : 2000);
    } while (ready < 0 && errno == EINTR);
    char response[48] = {0};
    ssize_t count = ready > 0 ? read(fd, response, sizeof(response) - 1) : -1;
    close(fd);
    if (count <= 0)
        return 1;
    fputs(response, stdout);
    if (stop)
        return strcmp(response, "stopped\n") ? 1 : 0;
    return !strcmp(response, "failed\n") ? 1 : 0;
}

int rv1126b_supervisor_main(int argc, char **argv) {
#ifdef RV1126B_BUILD_PROVENANCE
    extern const volatile char rv1126b_build_identity[];
    extern const volatile char rv1126b_build_source_identity[];
    (void)rv1126b_build_identity[0];
    (void)rv1126b_build_source_identity[0];
#endif
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("mybot-supervisor supervise --runtime DIR --log-dir DIR "
             "[--poweroff-command EXECUTABLE] [--factory-reset] -- EXECUTABLE [ARGS...]\n"
             "mybot-supervisor stop|status --runtime DIR");
        return 0;
    }
    if (argc < 4)
        return 2;
    const char *runtime = NULL, *logs = NULL, *poweroff = NULL;
    bool reset = false;
    int argument = 2;
    while (argument + 1 < argc && strcmp(argv[argument], "--")) {
        if (!strcmp(argv[argument], "--factory-reset")) {
            reset = true;
            ++argument;
            continue;
        }
        if (!strcmp(argv[argument], "--runtime"))
            runtime = argv[argument + 1];
        else if (!strcmp(argv[argument], "--log-dir"))
            logs = argv[argument + 1];
        else if (!strcmp(argv[argument], "--poweroff-command"))
            poweroff = argv[argument + 1];
        else
            return 2;
        argument += 2;
    }
    if (!strcmp(argv[1], "status") || !strcmp(argv[1], "stop")) {
        if (!runtime || logs || poweroff || reset || argument != argc)
            return 2;
        return rv1126b_supervisor_control(runtime, !strcmp(argv[1], "stop"));
    }
    if (strcmp(argv[1], "supervise") || !runtime || !logs || argument + 1 >= argc ||
        strcmp(argv[argument], "--"))
        return 2;
    rv1126b_supervisor_config_t config = {.runtime_directory = runtime,
                                          .log_directory = logs,
                                          .argv = argv + argument + 1,
                                          .poweroff_command = poweroff,
                                          .reset_directory = reset ? "/userdata/mybot" : NULL};
    return rv1126b_supervisor_run(&config);
}

#ifndef RV1126B_SUPERVISOR_NO_MAIN
int main(int argc, char **argv) {
    return rv1126b_supervisor_main(argc, argv);
}
#endif
