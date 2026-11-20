/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>

static int test_log_failure;
static int test_poweroff_log_ready;
static int test_supervisor_fcntl(int fd, int command, ...);
static int test_supervisor_dup2(int old_fd, int new_fd);
#define fcntl test_supervisor_fcntl
#define dup2 test_supervisor_dup2
#define RV1126B_SUPERVISOR_NO_MAIN
#define RV1126B_SUPERVISOR_FORCE_STATUS_SCAN
#define RV1126B_SUPERVISOR_TEST_ALLOW_UNPRIVILEGED
#include "../../src/platform/src/services/supervisor.c"
#undef fcntl
#undef dup2
#include "../../src/platform/src/services/power.c"
#include "check.h"
#include <dirent.h>

static char temporary[PATH_MAX], runtime_path[PATH_MAX], logs_path[PATH_MAX];
static char marker_path[PATH_MAX], executable_path[PATH_MAX];
static char power_command[PATH_MAX], power_marker[PATH_MAX];

static int test_supervisor_fcntl(int fd, int command, ...) {
    if (command == F_GETFL || command == F_GETFD)
        return fcntl(fd, command);
    va_list arguments;
    va_start(arguments, command);
    int value = va_arg(arguments, int);
    va_end(arguments);
    if (command == F_DUPFD_CLOEXEC && test_log_failure == 1) {
        errno = EMFILE;
        return -1;
    }
    int result = fcntl(fd, command, value);
    if (command == F_DUPFD_CLOEXEC && result >= 0)
        test_poweroff_log_ready = 1;
    return result;
}

static int test_supervisor_dup2(int old_fd, int new_fd) {
    if (test_log_failure == 2 && test_poweroff_log_ready) {
        errno = EMFILE;
        return -1;
    }
    return dup2(old_fd, new_fd);
}

static void pause_ms(unsigned int value) {
    struct timespec delay = {.tv_sec = value / 1000, .tv_nsec = (long)(value % 1000) * 1000000};
    nanosleep(&delay, NULL);
}

static void remove_tree(const char *path) {
    DIR *directory = opendir(path);
    CHECK(directory);
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char child[PATH_MAX];
        CHECK(snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) < (int)sizeof(child));
        struct stat status;
        CHECK(lstat(child, &status) == 0);
        if (S_ISDIR(status.st_mode))
            remove_tree(child);
        else
            CHECK(unlink(child) == 0);
    }
    CHECK(closedir(directory) == 0 && rmdir(path) == 0);
}

static void prepare(void) {
    strcpy(temporary, "/tmp/mybot-supervisor-test-XXXXXX");
    CHECK(mkdtemp(temporary));
    CHECK(snprintf(runtime_path, sizeof(runtime_path), "%s/runtime", temporary) > 0);
    CHECK(snprintf(logs_path, sizeof(logs_path), "%s/logs", temporary) > 0);
    CHECK(snprintf(marker_path, sizeof(marker_path), "%s/marker", temporary) > 0);
    CHECK(snprintf(power_command, sizeof(power_command), "%s/fake-poweroff", temporary) > 0);
    CHECK(snprintf(power_marker, sizeof(power_marker), "%s/powered-off", temporary) > 0);
    int fd = open(power_command, O_WRONLY | O_CREAT | O_EXCL, 0700);
    CHECK(fd >= 0);
    char script[PATH_MAX * 2 + 128];
    int length = snprintf(script, sizeof(script),
                          "#!/bin/sh\n[ ! -S '%s/control.sock' ] || exit 9\n"
                          "printf 'normal-poweroff\\n' > '%s'\n",
                          runtime_path, power_marker);
    CHECK(length > 0 && (size_t)length < sizeof(script));
    CHECK(write(fd, script, (size_t)length) == length && close(fd) == 0);
}

static void delayed_commit(const char *request, bool commit) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    const char *runtime = getenv("MYBOT_SUPERVISOR_RUNTIME");
    CHECK(runtime);
    CHECK(snprintf(address.sun_path, sizeof(address.sun_path), "%s/control.sock", runtime) <
          (int)sizeof(address.sun_path));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    CHECK(write(fd, request, strlen(request)) == (ssize_t)strlen(request));
    char response[16] = {0};
    CHECK(read(fd, response, sizeof(response) - 1) == 9);
    CHECK(!strcmp(response, "accepted\n"));
    if (commit) {
        pause_ms(300);
        CHECK(write(fd, "commit\n", 7) == 7);
        memset(response, 0, sizeof(response));
        CHECK(read(fd, response, sizeof(response) - 1) == 10);
        CHECK(!strcmp(response, "committed\n"));
    }
    CHECK(close(fd) == 0);
}

static int child_payload(const char *mode, const char *marker) {
    int fd = open(marker, O_WRONLY | O_CREAT | O_APPEND, 0600);
    CHECK(fd >= 0);
    if (!strncmp(mode, "reset-", 6)) {
        struct stat status;
        CHECK(fstat(fd, &status) == 0);
        char pending[PATH_MAX];
        /* The harness passes a fresh private directory in place of /userdata/mybot. */
        CHECK(snprintf(pending, sizeof(pending), "%s/../factory-reset.pending",
                       getenv("MYBOT_SUPERVISOR_RUNTIME")) < (int)sizeof(pending));
        if (!strcmp(mode, "reset-clean") && status.st_size > 0) {
            CHECK(lstat(pending, &status) == 0 && S_ISREG(status.st_mode));
            CHECK(unlink(pending) == 0);
            CHECK(write(fd, "relaunched\n", 11) == 11);
            close(fd);
            return 0;
        }
        if (!strcmp(mode, "reset-no-commit")) {
            delayed_commit("factory_reset\n", false);
            for (;;)
                pause();
        }
        int result = rv1126b_factory_reset_request();
        if (!strcmp(mode, "reset-other-station")) {
            CHECK(result < 0 && write(fd, "rejected\n", 9) == 9);
            close(fd);
            for (;;)
                pause();
        }
        if (!strcmp(mode, "reset-disabled") || !strcmp(mode, "reset-unsafe")) {
            close(fd);
            return result < 0 ? 0 : 8;
        }
        CHECK(result == 0);
        CHECK(lstat(pending, &status) == 0 && S_ISREG(status.st_mode) && status.st_nlink == 1 &&
              status.st_uid == geteuid() && (status.st_mode & 07777) == 0600 &&
              status.st_size == 0);
        CHECK(write(fd, "committed\n", 10) == 10);
        close(fd);
        return !strcmp(mode, "reset-fail") ? 1 : 0;
    }
    if (!strncmp(mode, "power-", 6)) {
        close(fd);
        if (!strcmp(mode, "power-no-commit")) {
            delayed_commit("poweroff\n", false);
            for (;;)
                pause();
        }
        if (!strcmp(mode, "power-delayed-commit")) {
            delayed_commit("poweroff\n", true);
            return 0;
        }
        int accepted = rv1126b_poweroff_request();
        if (!strcmp(mode, "power-disabled"))
            return accepted < 0 ? 0 : 8;
        CHECK(accepted == 0);
        if (!strcmp(mode, "power-nonzero"))
            return 7;
        if (!strcmp(mode, "power-crash")) {
            CHECK(kill(getpid(), SIGKILL) == 0);
            return 1;
        }
        if (!strcmp(mode, "power-hang")) {
            signal(SIGTERM, SIG_IGN);
            for (;;)
                pause();
        }
#ifdef __linux__
        if (!strcmp(mode, "power-descendant")) {
            pid_t descendant = fork();
            CHECK(descendant >= 0);
            if (!descendant) {
                CHECK(setsid() >= 0);
                signal(SIGTERM, SIG_IGN);
                for (;;)
                    pause();
            }
            /* Ensure the helper has installed its deliberate refusal policy. */
            pause_ms(30);
        }
#endif
        return 0;
    }
    if (!strcmp(mode, "failure")) {
        CHECK(write(fd, "attempt\n", 8) == 8);
        close(fd);
        return 7;
    }
    if (!strcmp(mode, "exit")) {
        close(fd);
        return 0;
    }
    if (!strcmp(mode, "unclean")) {
        close(fd);
        return RV1126B_EXIT_UNCLEAN;
    }
    if (!strcmp(mode, "crash")) {
        close(fd);
        CHECK(kill(getpid(), SIGKILL) == 0);
        return 1;
    }
    if (!strcmp(mode, "logs")) {
        char bytes[2000];
        memset(bytes, 'L', sizeof(bytes));
        CHECK(write(STDOUT_FILENO, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    }
    if (!strcmp(mode, "descendant") || !strcmp(mode, "orphan")) {
        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            CHECK(setsid() >= 0);
            signal(SIGTERM, SIG_IGN);
            close(fd);
            for (;;)
                pause();
        }
        char text[80];
        int length = snprintf(text, sizeof(text), "%ld %ld\n", (long)getpid(), (long)child);
        CHECK(write(fd, text, (size_t)length) == length);
        close(fd);
        if (!strcmp(mode, "orphan"))
            return 0;
        signal(SIGTERM, SIG_IGN);
    } else {
        char text[80];
        int length = snprintf(text, sizeof(text), "%ld\n", (long)getpid());
        CHECK(write(fd, text, (size_t)length) == length);
        close(fd);
    }
    char input;
    CHECK(read(STDIN_FILENO, &input, 1) == 0);
    for (;;)
        pause();
}

static pid_t start_supervisor(const char *mode, size_t log_limit) {
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        test_log_failure = !strcmp(mode, "power-log-dup-failure")    ? 1
                           : !strcmp(mode, "power-log-dup2-failure") ? 2
                                                                     : 0;
        CHECK(setenv("MYBOT_SUPERVISOR_RUNTIME", runtime_path, 1) == 0);
        char *arguments[] = {executable_path, "--child", (char *)mode, marker_path, NULL};
        rv1126b_supervisor_config_t config = {.runtime_directory = runtime_path,
                                              .log_directory = logs_path,
                                              .argv = arguments,
                                              .terminate_timeout_ms = 100,
                                              .cleanup_timeout_ms = 2000,
                                              .restart_window_ms = 5000,
                                              .restart_delays_ms = {5, 10, 15},
                                              .log_limit_bytes = log_limit};
        if (!strncmp(mode, "power-", 6) && strcmp(mode, "power-disabled"))
            config.poweroff_command = power_command;
        CHECK(setenv("RV1126B_WIFI_INTERFACE",
                     !strcmp(mode, "reset-other-station") ? "wlan1" : "wlan0", 1) == 0);
        if (!strncmp(mode, "reset-", 6) && strcmp(mode, "reset-disabled"))
            config.reset_directory = temporary;
        _exit(rv1126b_supervisor_run(&config));
    }
    return pid;
}

static bool query_state(char *state, size_t capacity) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    CHECK(snprintf(address.sun_path, sizeof(address.sun_path), "%s/control.sock", runtime_path) <
          (int)sizeof(address.sun_path));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return false;
    }
    CHECK(write(fd, "status\n", 7) == 7);
    struct pollfd ready = {.fd = fd, .events = POLLIN};
    CHECK(poll(&ready, 1, 2000) > 0);
    ssize_t length = read(fd, state, capacity - 1);
    CHECK(length > 0);
    state[length] = '\0';
    close(fd);
    return true;
}

static void await_state(const char *expected) {
    uint64_t deadline = milliseconds() + 4000;
    while (milliseconds() < deadline) {
        char state[48];
        if (query_state(state, sizeof(state)) && !strcmp(state, expected))
            return;
        pause_ms(5);
    }
    CHECK(false);
}

static int await_exit(pid_t pid) {
    int status;
    uint64_t deadline = milliseconds() + 4000;
    while (milliseconds() < deadline) {
        pid_t reaped = waitpid(pid, &status, WNOHANG);
        CHECK(reaped >= 0);
        if (reaped == pid) {
            CHECK(WIFEXITED(status));
            return WEXITSTATUS(status);
        }
        pause_ms(5);
    }
    CHECK(false);
    return -1;
}

static void end_failed_fixture(pid_t pid) {
    CHECK(kill(pid, SIGKILL) == 0);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status));
}

static void read_marker(pid_t *first, pid_t *second) {
    uint64_t deadline = milliseconds() + 4000;
    while (milliseconds() < deadline) {
        FILE *file = fopen(marker_path, "r");
        if (file) {
            long one = 0, two = 0;
            int count = fscanf(file, "%ld %ld", &one, &two);
            fclose(file);
            if (count >= (second ? 2 : 1)) {
                *first = (pid_t)one;
                if (second)
                    *second = (pid_t)two;
                return;
            }
        }
        pause_ms(5);
    }
    CHECK(false);
}

static void test_stop_and_duplicate_owner(void) {
    prepare();
    CHECK(setenv("MYBOT_TEST_SECRET", "not-for-service-logs", 1) == 0);
    pid_t supervisor = start_supervisor("wait", 512 * 1024);
    await_state("running\n");
    pid_t child;
    read_marker(&child, NULL);
    CHECK(child > 1 && kill(child, 0) == 0);
    pid_t duplicate = start_supervisor("wait", 512 * 1024);
    CHECK(await_exit(duplicate) == 1);
    CHECK(rv1126b_supervisor_control(runtime_path, 0) == 0);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 0);
    CHECK(await_exit(supervisor) == 0);
    CHECK(kill(child, 0) < 0 && errno == ESRCH);
    CHECK(rv1126b_supervisor_control(runtime_path, 0) == 3);
    char log_path[PATH_MAX];
    CHECK(snprintf(log_path, sizeof(log_path), "%s/mybot.log", logs_path) > 0);
    FILE *file = fopen(log_path, "r");
    CHECK(file);
    char text[512] = {0};
    CHECK(fread(text, 1, sizeof(text) - 1, file) > 0);
    CHECK(!strstr(text, "not-for-service-logs") && !strstr(text, "MYBOT_TEST_SECRET"));
    fclose(file);
    CHECK(unsetenv("MYBOT_TEST_SECRET") == 0);
    remove_tree(temporary);
}

static void test_restart_budget(void) {
    prepare();
    pid_t supervisor = start_supervisor("failure", 512 * 1024);
    await_state("failed\n");
    CHECK(rv1126b_supervisor_control(runtime_path, 0) == 1);
    struct stat status;
    CHECK(stat(marker_path, &status) == 0 && status.st_size == 4 * 8);
    pause_ms(100);
    CHECK(stat(marker_path, &status) == 0 && status.st_size == 4 * 8);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 0);
    CHECK(await_exit(supervisor) == 0);
    remove_tree(temporary);
}

static void test_normal_exit_and_logs(void) {
    prepare();
    CHECK(await_exit(start_supervisor("exit", 512 * 1024)) == 0);
    remove_tree(temporary);
    prepare();
    pid_t supervisor = start_supervisor("logs", 128);
    await_state("running\n");
    pid_t child;
    read_marker(&child, NULL);
    pause_ms(100);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 0 && await_exit(supervisor) == 0);
    for (int i = 0; i < LOG_FILES; ++i) {
        char path[PATH_MAX];
        if (i)
            CHECK(snprintf(path, sizeof(path), "%s/mybot.log.%d", logs_path, i) > 0);
        else
            CHECK(snprintf(path, sizeof(path), "%s/mybot.log", logs_path) > 0);
        struct stat status;
        CHECK(stat(path, &status) == 0 && status.st_size <= 128);
        CHECK((status.st_mode & 0777) == 0600);
    }
    remove_tree(temporary);
}

static void test_descendant_cleanup(void) {
#ifdef __linux__
    for (int orphan = 0; orphan < 2; ++orphan) {
        prepare();
        pid_t foreign = fork();
        CHECK(foreign >= 0);
        if (foreign == 0) {
            signal(SIGTERM, SIG_DFL);
            for (;;)
                pause();
        }
        pid_t supervisor = start_supervisor(orphan ? "orphan" : "descendant", 512 * 1024);
        pid_t leader, descendant;
        read_marker(&leader, &descendant);
        if (!orphan) {
            await_state("running\n");
            CHECK(rv1126b_supervisor_control(runtime_path, 1) == 1);
            await_state("failed\n");
        } else {
            CHECK(await_exit(supervisor) == 0);
        }
        CHECK(kill(leader, 0) < 0 && errno == ESRCH);
        CHECK(kill(descendant, 0) < 0 && errno == ESRCH);
        if (!orphan)
            end_failed_fixture(supervisor);
        CHECK(kill(foreign, 0) == 0);
        CHECK(kill(foreign, SIGTERM) == 0);
        int status;
        CHECK(waitpid(foreign, &status, 0) == foreign);
        remove_tree(temporary);
    }
#endif
}

static void test_unclean_blocks_restart(const char *mode) {
    prepare();
    pid_t supervisor = start_supervisor(mode, 512 * 1024);
    await_state("failed\n");
    CHECK(rv1126b_supervisor_control(runtime_path, 0) == 1);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 1);
    char path[PATH_MAX];
    CHECK(snprintf(path, sizeof(path), "%s/unclean", runtime_path) > 0);
    struct stat status;
    CHECK(stat(path, &status) == 0 && (status.st_mode & 0777) == 0600);
    end_failed_fixture(supervisor);
    CHECK(rv1126b_supervisor_control(runtime_path, 0) == 1);
    supervisor = start_supervisor("wait", 512 * 1024);
    await_state("failed\n");
    /* Existing marker prevents even the first payload launch. */
    CHECK(stat(marker_path, &status) == 0 && status.st_size == 0);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 1);
    end_failed_fixture(supervisor);
    remove_tree(temporary);
}

static void test_unsafe_inputs(void) {
    prepare();
    CHECK(mkdir(runtime_path, 0755) == 0);
    CHECK(await_exit(start_supervisor("wait", 128)) == 1);
    CHECK(access(marker_path, F_OK) < 0);
    CHECK(chmod(runtime_path, 0700) == 0);
    char socket_file[PATH_MAX];
    CHECK(snprintf(socket_file, sizeof(socket_file), "%s/control.sock", runtime_path) > 0);
    CHECK(symlink("untouched", socket_file) == 0);
    CHECK(await_exit(start_supervisor("wait", 128)) == 1);
    struct stat status;
    CHECK(lstat(socket_file, &status) == 0 && S_ISLNK(status.st_mode));
    CHECK(access(marker_path, F_OK) < 0);
    CHECK(signal_owned(getpid(), SIGKILL, true) < 0 && errno == ECHILD);
    CHECK(signal_owned(0, SIGKILL, true) < 0);
    CHECK(signal_owned(1, SIGKILL, true) < 0);
    remove_tree(temporary);
}

static void test_planned_poweroff(void) {
    prepare();
    CHECK(await_exit(start_supervisor("power-clean", 128)) == 0);
    CHECK(access(power_marker, F_OK) == 0);
    remove_tree(temporary);

    prepare();
    CHECK(await_exit(start_supervisor("power-delayed-commit", 128)) == 0);
    CHECK(access(power_marker, F_OK) == 0);
    remove_tree(temporary);

    prepare();
    CHECK(await_exit(start_supervisor("power-disabled", 128)) == 0);
    CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
    remove_tree(temporary);

    prepare();
    CHECK(await_exit(start_supervisor("power-nonzero", 128)) == 1);
    CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
    remove_tree(temporary);

    const char *unclean_modes[] = {"power-crash", "power-hang"
#ifdef __linux__
                                   ,
                                   "power-descendant"
#endif
    };
    for (size_t i = 0; i < sizeof(unclean_modes) / sizeof(unclean_modes[0]); ++i) {
        prepare();
        pid_t supervisor = start_supervisor(unclean_modes[i], 128);
        await_state("failed\n");
        CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
        pause_ms(50);
        end_failed_fixture(supervisor);
        remove_tree(temporary);
    }
}

static void test_poweroff_peer_and_commit(void) {
    prepare();
    pid_t supervisor = start_supervisor("power-no-commit", 128);
    await_state("running\n");
    pause_ms(100);
    CHECK(setenv("MYBOT_SUPERVISOR_RUNTIME", runtime_path, 1) == 0);
    CHECK(rv1126b_poweroff_request() < 0);
    CHECK(unsetenv("MYBOT_SUPERVISOR_RUNTIME") == 0);
    await_state("running\n");
    CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 0 && await_exit(supervisor) == 0);
    CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
    remove_tree(temporary);
}

static void test_factory_reset(void) {
    char pending[PATH_MAX];
    prepare();
    CHECK(snprintf(pending, sizeof(pending), "%s/factory-reset.pending", temporary) <
          (int)sizeof(pending));
    CHECK(await_exit(start_supervisor("reset-clean", 8192)) == 0);
    FILE *file = fopen(marker_path, "r");
    char text[32] = {0};
    CHECK(file && fread(text, 1, sizeof(text) - 1, file) > 0 && fclose(file) == 0);
    CHECK(!strcmp(text, "committed\nrelaunched\n"));
    CHECK(access(pending, F_OK) < 0 && errno == ENOENT);
    CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
    remove_tree(temporary);

    prepare();
    CHECK(snprintf(pending, sizeof(pending), "%s/factory-reset.pending", temporary) <
          (int)sizeof(pending));
    pid_t other = start_supervisor("reset-other-station", 128);
    struct stat rejection;
    for (unsigned int i = 0;
         i < 500 && (stat(marker_path, &rejection) < 0 || rejection.st_size != 9); ++i)
        pause_ms(10);
    CHECK(stat(marker_path, &rejection) == 0 && rejection.st_size == 9);
    await_state("running\n");
    CHECK(access(pending, F_OK) < 0 && errno == ENOENT);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 0 && await_exit(other) == 0);
    CHECK(await_exit(start_supervisor("reset-disabled", 128)) == 0);
    CHECK(access(pending, F_OK) < 0 && errno == ENOENT);
    CHECK(symlink("unrelated", pending) == 0);
    CHECK(await_exit(start_supervisor("reset-unsafe", 128)) == 0);
    struct stat status;
    CHECK(lstat(pending, &status) == 0 && S_ISLNK(status.st_mode));
    remove_tree(temporary);

    prepare();
    CHECK(snprintf(pending, sizeof(pending), "%s/factory-reset.pending", temporary) <
          (int)sizeof(pending));
    pid_t supervisor = start_supervisor("reset-no-commit", 128);
    await_state("running\n");
    CHECK(setenv("MYBOT_SUPERVISOR_RUNTIME", runtime_path, 1) == 0);
    CHECK(rv1126b_factory_reset_request() < 0);
    CHECK(unsetenv("MYBOT_SUPERVISOR_RUNTIME") == 0);
    CHECK(access(pending, F_OK) < 0 && errno == ENOENT);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 0 && await_exit(supervisor) == 0);
    remove_tree(temporary);

    prepare();
    CHECK(snprintf(pending, sizeof(pending), "%s/factory-reset.pending", temporary) <
          (int)sizeof(pending));
    supervisor = start_supervisor("reset-fail", 128);
    await_state("failed\n");
    CHECK(lstat(pending, &status) == 0 && S_ISREG(status.st_mode));
    CHECK(stat(marker_path, &status) == 0 && status.st_size == 10);
    CHECK(rv1126b_supervisor_control(runtime_path, 1) == 1);
    end_failed_fixture(supervisor);
    remove_tree(temporary);
}

static void check_private_log(const char *message) {
    char path[PATH_MAX], text[2048] = {0};
    CHECK(snprintf(path, sizeof(path), "%s/mybot.log", logs_path) > 0);
    struct stat status;
    CHECK(stat(path, &status) == 0 && status.st_uid == geteuid() &&
          (status.st_mode & 0777) == 0600);
    FILE *file = fopen(path, "r");
    CHECK(file && fread(text, 1, sizeof(text) - 1, file) > 0);
    CHECK(fclose(file) == 0 && strstr(text, message));
}

static void test_poweroff_helper_diagnostics(void) {
    prepare();
    int fd = open(power_command, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0);
    char script[PATH_MAX * 2 + 256];
    int length = snprintf(script, sizeof(script),
                          "#!/bin/sh\n[ ! -S '%s/control.sock' ] || exit 9\n"
                          "printf 'helper-ran\\n' > '%s'\n"
                          "printf 'fake-poweroff-failure\\n' >&2\nexit 7\n",
                          runtime_path, power_marker);
    CHECK(length > 0 && (size_t)length < sizeof(script));
    CHECK(write(fd, script, (size_t)length) == length && close(fd) == 0);
    CHECK(await_exit(start_supervisor("power-helper-failure", 512 * 1024)) == 7);
    CHECK(access(power_marker, F_OK) == 0);
    CHECK(rv1126b_supervisor_control(runtime_path, 0) == 3);
    check_private_log("fake-poweroff-failure");
    remove_tree(temporary);

    const char *modes[] = {"power-log-dup-failure", "power-log-dup2-failure"};
    const char *messages[] = {"cannot preserve poweroff diagnostics",
                              "cannot attach poweroff diagnostics"};
    for (size_t index = 0; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        prepare();
        CHECK(await_exit(start_supervisor(modes[index], 512 * 1024)) == 1);
        CHECK(access(power_marker, F_OK) < 0 && errno == ENOENT);
        CHECK(rv1126b_supervisor_control(runtime_path, 0) == 3);
        check_private_log(messages[index]);
        remove_tree(temporary);
    }
}

int main(int argc, char **argv) {
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 4 && !strcmp(argv[1], "--child"))
        return child_payload(argv[2], argv[3]);
    CHECK(realpath(argv[0], executable_path));
    test_stop_and_duplicate_owner();
    test_restart_budget();
    test_normal_exit_and_logs();
    test_descendant_cleanup();
    test_unclean_blocks_restart("crash");
    test_unclean_blocks_restart("unclean");
    test_unsafe_inputs();
    test_planned_poweroff();
    test_poweroff_peer_and_commit();
    test_factory_reset();
    test_poweroff_helper_diagnostics();
    puts("Supervisor identity, cleanup, retry budget and planned fake poweroff: PASS");
    return 0;
}
