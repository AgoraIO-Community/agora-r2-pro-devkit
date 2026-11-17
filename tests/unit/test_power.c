/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "../../src/platform/src/services/power.c"
#include "check.h"

#include <signal.h>
#include <sys/wait.h>

static char runtime_path[96];

static void prepare(void) {
    strcpy(runtime_path, "/tmp/mybot-power-test-XXXXXX");
    CHECK(mkdtemp(runtime_path));
    CHECK(setenv("MYBOT_SUPERVISOR_RUNTIME", runtime_path, 1) == 0);
}

static int mock_listener(void) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    CHECK(snprintf(address.sun_path, sizeof(address.sun_path), "%s/control.sock", runtime_path) <
          (int)sizeof(address.sun_path));
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(listener >= 0 && bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    CHECK(chmod(address.sun_path, 0600) == 0 && listen(listener, 1) == 0);
    return listener;
}

static void clean(void) {
    char path[128];
    CHECK(snprintf(path, sizeof(path), "%s/control.sock", runtime_path) > 0);
    CHECK(unlink(path) == 0 || errno == ENOENT);
    CHECK(rmdir(runtime_path) == 0);
    CHECK(unsetenv("MYBOT_SUPERVISOR_RUNTIME") == 0);
}

static pid_t start_mock(const char *response, int timeout) {
    int listener = mock_listener();
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        alarm(5);
        int fd = accept(listener, NULL, NULL);
        CHECK(fd >= 0);
        close(listener);
        char request[10] = {0};
        size_t used = 0;
        while (used < 9) {
            ssize_t count = read(fd, request + used, 9 - used);
            CHECK(count > 0);
            used += (size_t)count;
        }
        CHECK(!strcmp(request, "poweroff\n"));
        if (timeout == 1) {
            struct timespec delay = {.tv_sec = 2};
            nanosleep(&delay, NULL);
        } else if (response) {
            CHECK(write(fd, response, strlen(response)) == (ssize_t)strlen(response));
            if (!strcmp(response, "accepted\n")) {
                char commit[8] = {0};
                used = 0;
                while (used < 7) {
                    ssize_t count = read(fd, commit + used, 7 - used);
                    CHECK(count > 0);
                    used += (size_t)count;
                }
                CHECK(!strcmp(commit, "commit\n"));
                if (timeout == 2) {
                    struct timespec delay = {.tv_sec = 2};
                    nanosleep(&delay, NULL);
                } else if (timeout == 3) {
                    CHECK(write(fd, "failed\n", 7) == 7);
                } else {
                    CHECK(write(fd, "committed\n", 10) == 10);
                }
            }
        }
        close(fd);
        _exit(0);
    }
    close(listener);
    return child;
}

static void finish_mock(pid_t child) {
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    clean();
}

static void test_acknowledgement(void) {
    prepare();
    pid_t child = start_mock("accepted\n", 0);
    CHECK(rv1126b_poweroff_request() == 0);
    finish_mock(child);

    prepare();
    child = start_mock("failed\n", 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == EACCES);
    finish_mock(child);

    prepare();
    child = start_mock(NULL, 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == EPROTO);
    finish_mock(child);

    prepare();
    child = start_mock("accepted\nextra", 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == EACCES);
    finish_mock(child);

    prepare();
    child = start_mock("accepted\n", 3);
    CHECK(rv1126b_poweroff_request() < 0 && errno == EACCES);
    finish_mock(child);
}

static void test_timeout(void) {
    prepare();
    pid_t child = start_mock(NULL, 1);
    int64_t started = power_now_ms();
    CHECK(rv1126b_poweroff_request() < 0 && errno == ETIMEDOUT);
    int64_t elapsed = power_now_ms() - started;
    CHECK(elapsed >= 1400 && elapsed < 2500);
    finish_mock(child);

    prepare();
    child = start_mock("accepted\n", 2);
    started = power_now_ms();
    CHECK(rv1126b_poweroff_request() < 0 && errno == ETIMEDOUT);
    elapsed = power_now_ms() - started;
    CHECK(elapsed >= 1400 && elapsed < 2500);
    finish_mock(child);
}

static void test_unsafe_or_missing_endpoint(void) {
    CHECK(unsetenv("MYBOT_SUPERVISOR_RUNTIME") == 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == ENOTCONN);
    CHECK(setenv("MYBOT_SUPERVISOR_RUNTIME", "relative", 1) == 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == ENOTCONN);
    prepare();
    CHECK(rv1126b_poweroff_request() < 0 && errno == ENOENT);
    int listener = mock_listener();
    CHECK(chmod(runtime_path, 0755) == 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == EACCES);
    CHECK(chmod(runtime_path, 0700) == 0);
    char path[128];
    CHECK(snprintf(path, sizeof(path), "%s/control.sock", runtime_path) > 0);
    CHECK(chmod(path, 0666) == 0);
    CHECK(rv1126b_poweroff_request() < 0 && errno == EACCES);
    close(listener);
    clean();
}

int main(void) {
    test_unsafe_or_missing_endpoint();
    test_acknowledgement();
    test_timeout();
    puts("Poweroff client endpoint ownership, bounded ACK and commit: PASS");
    return 0;
}
