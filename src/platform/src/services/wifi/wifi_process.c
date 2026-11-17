/* SPDX-License-Identifier: Apache-2.0 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "wifi_backend_internal.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

void wifi_child_init(wifi_child_t *child) {
    child->pid = -1;
    child->status = -1;
}

static bool overridden(const char *entry, const char *const *environment) {
    const char *equal = strchr(entry, '=');
    if (!equal)
        return false;
    for (size_t i = 0; environment && environment[i]; ++i)
        if (!strncmp(entry, environment[i], (size_t)(equal - entry) + 1))
            return true;
    return false;
}

bool wifi_child_start(wifi_child_t *child, const char *const *arguments, int output,
                      const char *const *environment, char *error) {
    if (child->pid > 0 || !arguments || !arguments[0]) {
        wifi_error(error, "Invalid Wi-Fi process state");
        return false;
    }
    child->status = -1;
    size_t count = 0, extras = 0;
    while (environ[count])
        ++count;
    while (environment && environment[extras])
        ++extras;
    char **envp = calloc(count + extras + 1, sizeof(*envp));
    if (!envp) {
        wifi_error(error, "Cannot allocate Wi-Fi helper environment");
        return false;
    }
    size_t used = 0;
    for (size_t i = 0; i < count; ++i)
        if (!overridden(environ[i], environment))
            envp[used++] = environ[i];
    for (size_t i = 0; i < extras; ++i)
        envp[used++] = (char *)environment[i];
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    bool actions_ready = posix_spawn_file_actions_init(&actions) == 0;
    bool attributes_ready = posix_spawnattr_init(&attributes) == 0;
    int result = actions_ready && attributes_ready ? 0 : ENOMEM;
    if (!result)
        result = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!result)
        result =
            posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (!result) {
        if (output >= 0)
            result = posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
        else
            result =
                posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    sigset_t empty, defaults;
    sigemptyset(&empty);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGPIPE);
    if (!result)
        result = posix_spawnattr_setsigmask(&attributes, &empty);
    if (!result)
        result = posix_spawnattr_setsigdefault(&attributes, &defaults);
    if (!result)
        result = posix_spawnattr_setpgroup(&attributes, 0);
    if (!result)
        result = posix_spawnattr_setflags(
            &attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (!result)
        result = posix_spawn(&child->pid, arguments[0], &actions, &attributes,
                             (char *const *)arguments, envp);
    if (actions_ready)
        posix_spawn_file_actions_destroy(&actions);
    if (attributes_ready)
        posix_spawnattr_destroy(&attributes);
    free(envp);
    if (result) {
        child->pid = -1;
        wifi_error(error, "Cannot execute Wi-Fi helper");
        return false;
    }
    return true;
}

/* Observe without reaping: even an exited leader reserves the process-group
 * number until all group signals are sent. Handles have one waiting owner. */
static int child_exited(wifi_child_t *child) {
    if (child->pid <= 0)
        return -1;
    siginfo_t information = {0};
    int result;
    do {
        result = waitid(P_PID, (id_t)child->pid, &information, WEXITED | WNOHANG | WNOWAIT);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        child->status = -1;
        if (errno == ECHILD)
            child->pid = -1;
        return -1;
    }
    return information.si_pid == child->pid ? 1 : 0;
}

/* Call only after child_exited proves the leader is still our unreaped child. */
static void child_finish(wifi_child_t *child) {
    kill(-child->pid, SIGKILL);
    kill(child->pid, SIGKILL);
    int status;
    pid_t result;
    do {
        result = waitpid(child->pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    child->status = result == child->pid ? status : -1;
    if (result == child->pid || (result < 0 && errno == ECHILD))
        child->pid = -1;
}

bool wifi_child_running(wifi_child_t *child) {
    const int exited = child_exited(child);
    if (exited > 0)
        child_finish(child);
    /* Unexpected observation failures retain ownership for a later stop. */
    return child->pid > 0;
}

bool wifi_child_succeeded(const wifi_child_t *child) {
    return child->status >= 0 && WIFEXITED(child->status) && !WEXITSTATUS(child->status);
}

void wifi_child_stop(wifi_child_t *child) {
    const int exited = child_exited(child);
    if (exited < 0)
        return;
    if (exited) {
        child_finish(child);
        return;
    }
    kill(-child->pid, SIGTERM);
    uint64_t deadline = wifi_now_ms() + 400;
    while (wifi_child_running(child) && wifi_now_ms() < deadline)
        wifi_sleep_ms(10);
    if (child_exited(child) >= 0)
        child_finish(child);
}

bool wifi_command(const char *const *arguments, unsigned timeout_ms, const wifi_cancel_t *cancel,
                  wifi_buffer_t *output, char *error) {
    output->length = 0;
    if (output->data)
        output->data[0] = 0;
    if (wifi_cancelled(cancel)) {
        wifi_error(error, "Wi-Fi operation cancelled");
        return false;
    }
    int descriptors[2];
    if (pipe(descriptors)) {
        wifi_error(error, "Cannot capture Wi-Fi helper output");
        return false;
    }
    fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
    fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);
    fcntl(descriptors[0], F_SETFL, O_NONBLOCK);
    wifi_child_t process;
    wifi_child_init(&process);
    if (!wifi_child_start(&process, arguments, descriptors[1], NULL, error)) {
        close(descriptors[0]);
        close(descriptors[1]);
        return false;
    }
    close(descriptors[1]);
    uint64_t deadline = wifi_now_ms() + timeout_ms;
    bool failed = false, running = true;
    while (running) {
        char bytes[4096];
        ssize_t used;
        while ((used = read(descriptors[0], bytes, sizeof(bytes))) > 0)
            if (output->length + (size_t)used > WIFI_COMMAND_OUTPUT_LIMIT ||
                !wifi_buffer_append(output, bytes, (size_t)used)) {
                failed = true;
                wifi_error(error, "Wi-Fi helper output exceeded limit");
                break;
            }
        if (failed || wifi_cancelled(cancel) || wifi_now_ms() >= deadline) {
            if (!failed)
                wifi_error(error, wifi_cancelled(cancel) ? "Wi-Fi operation cancelled"
                                                         : "Wi-Fi helper timed out");
            failed = true;
            break;
        }
        running = wifi_child_running(&process);
        if (!running) {
            while ((used = read(descriptors[0], bytes, sizeof(bytes))) > 0)
                if (output->length + (size_t)used > WIFI_COMMAND_OUTPUT_LIMIT ||
                    !wifi_buffer_append(output, bytes, (size_t)used)) {
                    failed = true;
                    wifi_error(error, "Wi-Fi helper output exceeded limit");
                    break;
                }
            break;
        }
        struct pollfd ready = {descriptors[0], POLLIN, 0};
        poll(&ready, 1, 20);
    }
    close(descriptors[0]);
    if (failed) {
        wifi_child_stop(&process);
        return false;
    }
    if (!wifi_child_succeeded(&process)) {
        wifi_error(error, "Wi-Fi helper failed");
        return false;
    }
    return true;
}

bool wifi_find_program(const char *program, char *path, size_t capacity) {
    static const char *const directories[] = {"/usr/sbin/", "/sbin/", "/usr/bin/", "/bin/"};
    for (size_t i = 0; i < sizeof(directories) / sizeof(*directories); ++i) {
        struct stat st;
        int used = snprintf(path, capacity, "%s%s", directories[i], program);
        if (used >= 0 && (size_t)used < capacity && !stat(path, &st) && S_ISREG(st.st_mode) &&
            !access(path, X_OK))
            return true;
    }
    if (capacity)
        path[0] = 0;
    return false;
}

bool wifi_read_text(const char *path, wifi_buffer_t *output) {
    output->length = 0;
    if (output->data)
        output->data[0] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    bool ok = true;
    char bytes[4096];
    for (;;) {
        ssize_t used = read(fd, bytes, sizeof(bytes));
        if (used < 0 && errno == EINTR)
            continue;
        if (used < 0) {
            ok = false;
            break;
        }
        if (!used)
            break;
        if (!wifi_buffer_append(output, bytes, (size_t)used)) {
            ok = false;
            break;
        }
    }
    close(fd);
    return ok;
}

static void program_basename(const char *input, char *output, size_t capacity) {
    wifi_copy(output, capacity, input ? input : "");
    size_t length = strlen(output);
    const char deleted[] = " (deleted)";
    if (length >= sizeof(deleted) - 1 && !strcmp(output + length - sizeof(deleted) + 1, deleted))
        output[length - sizeof(deleted) + 1] = 0;
    char *slash = strrchr(output, '/');
    if (slash)
        memmove(output, slash + 1, strlen(slash + 1) + 1);
}

bool wifi_process_identity_uses_interface(const char *program, const char *interface_name,
                                          const char *executable, const char *comm,
                                          const char *const *arguments) {
    if (!arguments || !arguments[0])
        return false;
    char name[4096];
    program_basename(arguments[0], name, sizeof(name));
    if (!strcmp(program, "dhcpcd") && !strncmp(name, "dhcpcd:", 7)) {
        char executable_name[4096];
        program_basename(executable, executable_name, sizeof(executable_name));
        if (strcmp(executable_name, "dhcpcd") && strcmp(comm ? comm : "", "dhcpcd"))
            return false;
        const char *title = name + 7;
        while (*title == ' ')
            ++title;
        static const char *const roles[] = {"[manager]", "[privileged proxy]", "[network proxy]"};
        for (size_t i = 0; i < sizeof(roles) / sizeof(*roles); ++i) {
            size_t length = strlen(roles[i]);
            if (!strncmp(title, roles[i], length) && (!title[length] || title[length] == ' '))
                return true;
        }
        return false;
    }
    if (strcmp(name, program))
        return false;
    bool specific = false, matches = false;
    for (size_t i = 1; arguments[i]; ++i) {
        if (!strcmp(arguments[i], "-i") && arguments[i + 1]) {
            specific = true;
            matches |= !strcmp(arguments[i + 1], interface_name);
        } else if (!strncmp(arguments[i], "-i", 2) && arguments[i][2]) {
            specific = true;
            matches |= !strcmp(arguments[i] + 2, interface_name);
        }
    }
    return !specific || matches;
}

bool wifi_process_uses_interface(const char *program, const char *interface_name) {
    DIR *directory = opendir("/proc");
    if (!directory)
        return true;
    bool conflict = false;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        bool digits = entry->d_name[0] != 0;
        for (const char *p = entry->d_name; *p; ++p)
            digits &= isdigit((unsigned char)*p) != 0;
        if (!digits)
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        wifi_buffer_t bytes;
        wifi_buffer_init(&bytes, 65536);
        if (!wifi_read_text(path, &bytes) || !bytes.length) {
            wifi_buffer_free(&bytes);
            continue;
        }
        const char *arguments[257];
        size_t count = 0, offset = 0;
        while (offset < bytes.length && count < 256) {
            arguments[count++] = bytes.data + offset;
            size_t remaining = bytes.length - offset;
            size_t length = strnlen(bytes.data + offset, remaining);
            offset += length + 1;
        }
        arguments[count] = NULL;
        char executable[PATH_MAX] = "", comm[256] = "";
        if (!strcmp(program, "dhcpcd")) {
            snprintf(path, sizeof(path), "/proc/%s/exe", entry->d_name);
            ssize_t length = readlink(path, executable, sizeof(executable) - 1);
            if (length > 0)
                executable[length] = 0;
            snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
            wifi_buffer_t value;
            wifi_buffer_init(&value, 255);
            if (wifi_read_text(path, &value) && value.data) {
                wifi_copy(comm, sizeof(comm), value.data);
                comm[strcspn(comm, "\r\n")] = 0;
            }
            wifi_buffer_free(&value);
        }
        conflict = wifi_process_identity_uses_interface(program, interface_name, executable, comm,
                                                        arguments);
        wifi_buffer_free(&bytes);
        if (conflict)
            break;
    }
    closedir(directory);
    return conflict;
}
