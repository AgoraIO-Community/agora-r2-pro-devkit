/* SPDX-License-Identifier: Apache-2.0 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <signal.h>
#include <sys/types.h>

static int recorded_kill(pid_t target, int signal_number);

/* Execute real process-group signals, while proving stale/non-child handles
 * never reach kill(). Keep this probe out of the production API. */
#define kill recorded_kill
#include "../../src/platform/src/services/wifi/wifi_process.c"
#undef kill
#include "check.h"

static unsigned signals_sent, groups_killed;

static int recorded_kill(pid_t target, int signal_number) {
    ++signals_sent;
    if (target < 0 && signal_number == SIGKILL)
        ++groups_killed;
    return kill(target, signal_number);
}

static void exit_on_term(int signal_number) {
    (void)signal_number;
    _exit(23);
}

static int group_fixture(const char *mode) {
    alarm(10);
    if (!strcmp(mode, "term-exit"))
        CHECK(signal(SIGTERM, exit_on_term) != SIG_ERR);
    else if (!strcmp(mode, "ignore-term"))
        CHECK(signal(SIGTERM, SIG_IGN) != SIG_ERR);
    int ready[2];
    CHECK(pipe(ready) == 0);
    pid_t descendant = fork();
    CHECK(descendant >= 0);
    if (!descendant) {
        alarm(10);
        CHECK(close(ready[0]) == 0 && close(STDOUT_FILENO) == 0);
        CHECK(signal(SIGTERM, SIG_IGN) != SIG_ERR);
        CHECK(write(ready[1], "1", 1) == 1 && close(ready[1]) == 0);
        for (;;)
            pause();
    }
    CHECK(close(ready[1]) == 0);
    char byte;
    CHECK(read(ready[0], &byte, 1) == 1 && close(ready[0]) == 0);
    CHECK(write(STDOUT_FILENO, &descendant, sizeof(descendant)) == (ssize_t)sizeof(descendant));
    CHECK(close(STDOUT_FILENO) == 0);
    if (!strcmp(mode, "exit-zero"))
        return 0;
    if (!strcmp(mode, "exit-failure"))
        return 7;
    for (;;)
        pause();
}

static bool process_live(pid_t pid) {
    if (kill(pid, 0) < 0 && errno == ESRCH)
        return false;
#if defined(__linux__)
    /* PID 1 may retain a killed orphan as a zombie, which cannot execute. */
    char path[64], text[256];
    CHECK(snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid) > 0);
    FILE *file = fopen(path, "r");
    if (file) {
        if (fgets(text, sizeof(text), file)) {
            char *end = strrchr(text, ')');
            if (end && end[1] == ' ' && end[2] == 'Z') {
                CHECK(fclose(file) == 0);
                return false;
            }
        }
        CHECK(fclose(file) == 0);
    }
#endif
    return true;
}

static void expect_stopped(pid_t descendant) {
    const uint64_t deadline = wifi_now_ms() + 2000;
    while (process_live(descendant) && wifi_now_ms() < deadline)
        wifi_sleep_ms(1);
    CHECK(!process_live(descendant));
}

static pid_t start_fixture(wifi_child_t *child, const char *self, const char *mode) {
    int output[2];
    CHECK(pipe(output) == 0);
    CHECK(fcntl(output[0], F_SETFD, FD_CLOEXEC) == 0);
    CHECK(fcntl(output[1], F_SETFD, FD_CLOEXEC) == 0);
    const char *arguments[] = {self, "--group-fixture", mode, NULL};
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_child_start(child, arguments, output[1], NULL, error));
    CHECK(!wifi_child_succeeded(child));
    CHECK(close(output[1]) == 0);
    struct pollfd ready = {output[0], POLLIN, 0};
    CHECK(poll(&ready, 1, 5000) == 1);
    pid_t descendant;
    CHECK(read(output[0], &descendant, sizeof(descendant)) == (ssize_t)sizeof(descendant));
    CHECK(close(output[0]) == 0 && descendant > 1 && process_live(descendant));
    return descendant;
}

static void wait_exited(pid_t leader) {
    const uint64_t deadline = wifi_now_ms() + 5000;
    siginfo_t information = {0};
    do {
        CHECK(waitid(P_PID, (id_t)leader, &information, WEXITED | WNOHANG | WNOWAIT) == 0);
        if (information.si_pid == leader)
            return;
        wifi_sleep_ms(1);
    } while (wifi_now_ms() < deadline);
    CHECK(information.si_pid == leader);
}

static void natural_exit(const char *self, bool success, bool use_stop) {
    wifi_child_t child;
    wifi_child_init(&child);
    const pid_t descendant = start_fixture(&child, self, success ? "exit-zero" : "exit-failure");
    const pid_t leader = child.pid;
    wait_exited(leader);
    const unsigned before = groups_killed;
    if (use_stop)
        wifi_child_stop(&child);
    else
        CHECK(!wifi_child_running(&child));
    CHECK(child.pid == -1 && groups_killed > before);
    CHECK(WIFEXITED(child.status) && WEXITSTATUS(child.status) == (success ? 0 : 7));
    CHECK(wifi_child_succeeded(&child) == success);
    expect_stopped(descendant);
    CHECK(waitpid(leader, NULL, WNOHANG) < 0 && errno == ECHILD);
    const int original_status = child.status;
    const unsigned original_signals = signals_sent;
    wifi_child_stop(&child);
    CHECK(child.status == original_status && signals_sent == original_signals);
}

static void stop_running_group(const char *self, bool ignores_term) {
    wifi_child_t child;
    wifi_child_init(&child);
    const pid_t descendant =
        start_fixture(&child, self, ignores_term ? "ignore-term" : "term-exit");
    const pid_t leader = child.pid;
    CHECK(getpgid(leader) == leader && getpgid(descendant) == leader);
    CHECK(wifi_child_running(&child));
    wifi_child_stop(&child);
    CHECK(child.pid == -1 && !wifi_child_succeeded(&child));
    if (ignores_term)
        CHECK(WIFSIGNALED(child.status) && WTERMSIG(child.status) == SIGKILL);
    else
        CHECK(WIFEXITED(child.status) && WEXITSTATUS(child.status) == 23);
    expect_stopped(descendant);
    CHECK(waitpid(leader, NULL, WNOHANG) < 0 && errno == ECHILD);
}

static void command_cleans_descendants(const char *self, bool success) {
    const char *arguments[] = {self, "--group-fixture", success ? "exit-zero" : "exit-failure",
                               NULL};
    wifi_buffer_t output;
    wifi_buffer_init(&output, WIFI_COMMAND_OUTPUT_LIMIT);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_command(arguments, 5000, NULL, &output, error) == success);
    CHECK(output.length == sizeof(pid_t));
    pid_t descendant;
    memcpy(&descendant, output.data, sizeof(descendant));
    expect_stopped(descendant);
    if (!success)
        CHECK(strstr(error, "failed"));
    wifi_buffer_free(&output);
}

static void stale_or_foreign_handle(const char *self) {
    const char *arguments[] = {self, "--exit", NULL};
    wifi_child_t child;
    wifi_child_init(&child);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_child_start(&child, arguments, -1, NULL, error));
    CHECK(waitpid(child.pid, NULL, 0) == child.pid);
    const unsigned before = signals_sent;
    wifi_child_stop(&child);
    CHECK(child.pid == -1 && child.status == -1 && signals_sent == before);

    const pid_t foreign[] = {getpid(), getppid(), 1};
    for (size_t i = 0; i < sizeof(foreign) / sizeof(foreign[0]); ++i) {
        child.pid = foreign[i];
        child.status = 0;
        CHECK(!wifi_child_running(&child));
        CHECK(child.pid == -1 && child.status == -1);
        wifi_child_stop(&child);
        CHECK(signals_sent == before);
        child.pid = foreign[i];
        wifi_child_stop(&child);
        CHECK(child.pid == -1 && signals_sent == before);
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--exit"))
        return 0;
    if (argc == 3 && !strcmp(argv[1], "--group-fixture"))
        return group_fixture(argv[2]);
    char self[PATH_MAX];
    CHECK(realpath(argv[0], self));
    natural_exit(self, true, true);
    natural_exit(self, false, true);
    natural_exit(self, true, false);
    natural_exit(self, false, false);
    stop_running_group(self, false);
    stop_running_group(self, true);
    command_cleans_descendants(self, true);
    command_cleans_descendants(self, false);
    stale_or_foreign_handle(self);
    CHECK(waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD);
    puts("Wi-Fi owned groups, retained exit status and stale-handle isolation: PASS");
    return 0;
}
