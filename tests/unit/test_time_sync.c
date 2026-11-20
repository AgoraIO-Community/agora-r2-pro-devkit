/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/time_sync.h"
#include "time_sync_internal.h"
#include "check.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VALID_TIME INT64_C(1767225600)

typedef struct {
    int64_t realtime;
    uint64_t monotonic;
    int realtime_calls, monotonic_calls, service_calls, spawn_calls, inspect_calls;
    int cancel_calls, reap_calls;
    int clock_error, monotonic_error, service_result, spawn_error;
    bool exited;
    int pending_reaps;
    pid_t spawned_pid, owned_pid;
} fake_t;

static int fake_realtime(void *context, int64_t *seconds) {
    fake_t *fake = context;
    ++fake->realtime_calls;
    *seconds = fake->realtime;
    return fake->clock_error ? -1 : 0;
}

static int fake_monotonic(void *context, uint64_t *milliseconds) {
    fake_t *fake = context;
    ++fake->monotonic_calls;
    *milliseconds = fake->monotonic;
    return fake->monotonic_error ? -1 : 0;
}

static int fake_service(void *context) {
    fake_t *fake = context;
    ++fake->service_calls;
    return fake->service_result;
}

static int fake_spawn(void *context, const char *path, pid_t *pid) {
    fake_t *fake = context;
    CHECK(path[0] == '/' && !fake->owned_pid);
    ++fake->spawn_calls;
    if (fake->spawn_error) {
        return -1;
    }
    *pid = fake->spawned_pid;
    if (*pid > 1) {
        fake->owned_pid = *pid;
    }
    return 0;
}

static int fake_exited(void *context, pid_t pid) {
    fake_t *fake = context;
    CHECK(pid > 1 && pid == fake->owned_pid);
    ++fake->inspect_calls;
    return fake->exited ? 1 : 0;
}

static int fake_cancel(void *context, pid_t pid) {
    fake_t *fake = context;
    CHECK(pid > 1 && pid == fake->owned_pid);
    ++fake->cancel_calls;
    return 0;
}

static int fake_reap(void *context, pid_t pid, bool block) {
    fake_t *fake = context;
    CHECK(pid > 1 && pid == fake->owned_pid && fake->cancel_calls > 0);
    ++fake->reap_calls;
    if (!block && fake->pending_reaps > 0) {
        --fake->pending_reaps;
        return 0;
    }
    fake->owned_pid = 0;
    return 1;
}

static const rv1126b_time_sync_backend_t fake_backend = {
    .realtime = fake_realtime,
    .monotonic = fake_monotonic,
    .service_running = fake_service,
    .spawn_helper = fake_spawn,
    .helper_exited = fake_exited,
    .cancel_helper = fake_cancel,
    .reap_helper = fake_reap,
};

static fake_t initial_fake(void) {
    fake_t fake = {.monotonic = 1000, .spawned_pid = 43210};
    return fake;
}

static rv1126b_time_sync_t *create(fake_t *fake, unsigned int timeout, const char *helper) {
    rv1126b_time_sync_config_t config = {.timeout_seconds = timeout, .helper_path = helper};
    rv1126b_time_sync_t *sync = rv1126b_time_sync_create_with_backend(&config, &fake_backend, fake);
    CHECK(sync);
    return sync;
}

static void test_ready_and_network_gate(void) {
    fake_t fake = initial_fake();
    fake.realtime = VALID_TIME;
    rv1126b_time_sync_t *sync = create(&fake, 30, "/missing/not-inspected");
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    CHECK(fake.realtime_calls == 0 && fake.monotonic_calls == 0 && fake.service_calls == 0 &&
          fake.spawn_calls == 0);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_READY);
    CHECK(fake.realtime_calls == 1 && fake.monotonic_calls == 0 && fake.service_calls == 0 &&
          fake.spawn_calls == 0 && !*rv1126b_time_sync_error(sync));
    /* A plausible clock must not let SDK startup bypass a lost route or AP mode. */
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_READY);
    fake.realtime = VALID_TIME - 1;
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    CHECK(fake.spawn_calls == 1);
    rv1126b_time_sync_destroy(sync);
    CHECK(fake.cancel_calls == 1 && fake.reap_calls == 1 && !fake.owned_pid);
}

static void test_os_wait_deadline_and_recovery(void) {
    fake_t fake = initial_fake();
    fake.service_result = -1;
    rv1126b_time_sync_t *sync = create(&fake, 30, "");
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    fake.monotonic += 900000;
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    CHECK(fake.monotonic_calls == 0);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    fake.monotonic += 29999;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    ++fake.monotonic;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_TIMEOUT);
    CHECK(strstr(rv1126b_time_sync_error(sync), "NTP"));
    CHECK(fake.service_calls == 0 && fake.spawn_calls == 0 && fake.cancel_calls == 0);
    int initial_reads = fake.realtime_calls;
    fake.monotonic += 999;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_TIMEOUT);
    CHECK(fake.realtime_calls == initial_reads);
    ++fake.monotonic;
    fake.realtime = VALID_TIME;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_READY);
    CHECK(!*rv1126b_time_sync_error(sync));
    rv1126b_time_sync_destroy(sync);

    fake = initial_fake();
    sync = create(&fake, 1, NULL);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    fake.monotonic += 900;
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    fake.monotonic += 900000;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    fake.monotonic += 999;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    ++fake.monotonic;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_TIMEOUT);
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    rv1126b_time_sync_destroy(sync);
}

static void test_helper_lifecycle_and_external_service(void) {
    fake_t fake = initial_fake();
    fake.service_result = 1;
    rv1126b_time_sync_t *sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    CHECK(fake.service_calls == 1 && fake.spawn_calls == 0);
    fake.monotonic += 30000;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_TIMEOUT);
    CHECK(fake.cancel_calls == 0); /* An existing system daemon is never ours. */
    rv1126b_time_sync_destroy(sync);

    fake = initial_fake();
    sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    CHECK(fake.spawn_calls == 1 && fake.owned_pid == 43210);
    fake.realtime = VALID_TIME;
    fake.pending_reaps = 1;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    CHECK(fake.cancel_calls == 1 && fake.owned_pid == 43210);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_READY);
    CHECK(fake.cancel_calls == 1 && !fake.owned_pid);
    rv1126b_time_sync_destroy(sync);

    fake = initial_fake();
    sync = create(&fake, 1, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    fake.monotonic += 1000;
    fake.pending_reaps = 1;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_TIMEOUT);
    CHECK(fake.cancel_calls == 1 && !fake.owned_pid);
    rv1126b_time_sync_destroy(sync);

    fake = initial_fake();
    sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    fake.exited = true;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    CHECK(strstr(rv1126b_time_sync_error(sync), "exited"));
    CHECK(fake.cancel_calls == 1 && !fake.owned_pid);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    CHECK(fake.spawn_calls == 1);
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    fake.exited = false;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    CHECK(fake.spawn_calls == 2);
    CHECK(rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
    CHECK(fake.cancel_calls == 2 && !fake.owned_pid);
    rv1126b_time_sync_destroy(sync);
}

static void test_failures_and_configuration(void) {
    rv1126b_time_sync_config_t config = {.timeout_seconds = 0};
    CHECK(!rv1126b_time_sync_create(&config));
    config.timeout_seconds = 301;
    CHECK(!rv1126b_time_sync_create(&config));
    config.timeout_seconds = 30;
    config.helper_path = "relative-helper";
    CHECK(!rv1126b_time_sync_create(&config));
    config.helper_path = NULL;
    rv1126b_time_sync_t *sync = rv1126b_time_sync_create(NULL);
    CHECK(sync);
    rv1126b_time_sync_destroy(sync);
    rv1126b_time_sync_destroy(NULL);
    CHECK(rv1126b_time_sync_poll(NULL, true) == RV1126B_TIME_FAILED);
    CHECK(*rv1126b_time_sync_error(NULL));

    fake_t fake = initial_fake();
    fake.service_result = -1;
    sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    CHECK(strstr(rv1126b_time_sync_error(sync), "/proc"));
    CHECK(fake.spawn_calls == 0 && fake.cancel_calls == 0);
    rv1126b_time_sync_destroy(sync);
    fake = initial_fake();
    fake.spawn_error = 1;
    sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    CHECK(strstr(rv1126b_time_sync_error(sync), "executable"));
    CHECK(fake.cancel_calls == 0);
    rv1126b_time_sync_destroy(sync);
    fake = initial_fake();
    fake.spawned_pid = 1;
    sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    rv1126b_time_sync_destroy(sync);
    CHECK(fake.cancel_calls == 0 && fake.reap_calls == 0);

    fake = initial_fake();
    sync = create(&fake, 30, "/explicit/helper");
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    fake.clock_error = 1;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    CHECK(fake.cancel_calls == 1 && !fake.owned_pid);
    rv1126b_time_sync_destroy(sync);
    fake = initial_fake();
    fake.monotonic_error = 1;
    sync = create(&fake, 30, NULL);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    rv1126b_time_sync_destroy(sync);
    fake = initial_fake();
    sync = create(&fake, 300, NULL);
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
    --fake.monotonic;
    CHECK(rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
    CHECK(strstr(rv1126b_time_sync_error(sync), "backwards"));
    rv1126b_time_sync_destroy(sync);
}

static void test_service_process_names(void) {
    CHECK(rv1126b_time_sync_process_is_service("ntpd", NULL, 0));
    CHECK(rv1126b_time_sync_process_is_service("chronyd", NULL, 0));
    CHECK(rv1126b_time_sync_process_is_service("openntpd", NULL, 0));
    CHECK(rv1126b_time_sync_process_is_service("systemd-timesyn", NULL, 0));
    CHECK(rv1126b_time_sync_process_is_service("systemd-timesyncd", NULL, 0));
    const char busybox_ntp[] = "/bin/busybox\0ntpd\0-n\0-q\0";
    CHECK(rv1126b_time_sync_process_is_service("busybox", busybox_ntp, sizeof(busybox_ntp)));
    const char applet_name[] = "/usr/sbin/ntpd\0-n\0";
    CHECK(rv1126b_time_sync_process_is_service("busybox", applet_name, sizeof(applet_name)));
    const char different_applet[] = "/bin/busybox\0sleep\0ntpd\0";
    CHECK(!rv1126b_time_sync_process_is_service("busybox", different_applet,
                                                sizeof(different_applet)));
    const char shell_command[] = "/bin/sh\0-c\0ntpd -n\0";
    CHECK(!rv1126b_time_sync_process_is_service("sh", shell_command, sizeof(shell_command)));
    const char truncated[] = "/bin/busybox\0ntpd";
    CHECK(!rv1126b_time_sync_process_is_service("busybox", truncated, sizeof(truncated) - 1));
    CHECK(!rv1126b_time_sync_process_is_service("busybox", "busybox", 7));
    CHECK(!rv1126b_time_sync_process_is_service("busybox", NULL, 0));
    CHECK(!rv1126b_time_sync_process_is_service(NULL, "\0ntpd\0", 6));
}

static uint64_t real_monotonic(void) {
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void pause_one_ms(void) {
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
    nanosleep(&pause, NULL);
}

static int actual_spawn(void *context, const char *path, pid_t *pid) {
    fake_t *fake = context;
    ++fake->spawn_calls;
    int result = rv1126b_time_sync_default_backend()->spawn_helper(NULL, path, pid);
    if (result == 0) {
        fake->spawned_pid = *pid;
    }
    return result;
}

static bool process_live(pid_t pid) {
    if (kill(pid, 0) < 0 && errno == ESRCH) {
        return false;
    }
#if defined(__linux__)
    /* A killed orphan may remain a zombie until PID 1 reaps it. It cannot run. */
    char path[64], text[256];
    CHECK(snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid) > 0);
    FILE *file = fopen(path, "r");
    if (file) {
        if (fgets(text, sizeof(text), file)) {
            char *end = strrchr(text, ')');
            if (end && end[1] == ' ' && end[2] == 'Z') {
                fclose(file);
                return false;
            }
        }
        fclose(file);
    }
#endif
    return true;
}

static void fixture_failure(rv1126b_time_sync_t **sync, const char *condition, int line) {
    /* A failed test must still kill/reap the verified owned process group.
     * Otherwise its sleep descendant inherits CTest's pipe after abort and
     * makes an ordinary assertion failure look like a 30-second test timeout. */
    rv1126b_time_sync_destroy(*sync);
    *sync = NULL;
    fprintf(stderr, "%s:%d: %s\n", __FILE__, line, condition);
    abort();
}

#define FIXTURE_CHECK(sync, condition)                                                             \
    do {                                                                                           \
        if (!(condition))                                                                          \
            fixture_failure(&(sync), #condition, __LINE__);                                        \
    } while (0)

static void test_real_owned_process_group(void) {
    const rv1126b_time_sync_backend_t *production = rv1126b_time_sync_default_backend();
    /* Even a positive PID cannot authorize signalling unless it is our child. */
    CHECK(production->cancel_helper(NULL, 0) < 0);
    CHECK(production->cancel_helper(NULL, 1) < 0);
    CHECK(production->cancel_helper(NULL, -1) < 0);
    CHECK(production->cancel_helper(NULL, getpid()) < 0 && errno == ECHILD);

    char directory[] = "/tmp/mybot-time-test-XXXXXX";
    CHECK(mkdtemp(directory));
    char helper[512], pids[512];
    CHECK(snprintf(helper, sizeof(helper), "%s/helper.sh", directory) > 0);
    CHECK(snprintf(pids, sizeof(pids), "%s/pids", directory) > 0);
    for (int exited_leader = 0; exited_leader < 2; ++exited_leader) {
        FILE *file = fopen(helper, "w");
        CHECK(file);
        CHECK(fprintf(file,
                      "#!/bin/sh\n[ \"$#\" -eq 0 ] || exit 9\n"
                      "IFS= read -r unwanted && exit 8\n/bin/sleep 30 &\n"
                      "child=$!\nprintf '%%s %%s\\n' \"$$\" \"$child\" > '%s'\n%s\n",
                      pids, exited_leader ? "exit 0" : "wait") > 0);
        CHECK(fclose(file) == 0 && chmod(helper, 0700) == 0);
        fake_t fake = initial_fake();
        rv1126b_time_sync_backend_t backend = *production;
        backend.realtime = fake_realtime;
        backend.monotonic = fake_monotonic;
        backend.service_running = fake_service;
        backend.spawn_helper = actual_spawn;
        rv1126b_time_sync_config_t config = {.timeout_seconds = 30, .helper_path = helper};
        rv1126b_time_sync_t *sync = rv1126b_time_sync_create_with_backend(&config, &backend, &fake);
        CHECK(sync);
        /* The helper must see EOF without consuming the application's input. */
        int input[2];
        CHECK(pipe(input) == 0);
        CHECK(write(input[1], "keep\n", 5) == 5 && close(input[1]) == 0);
        int saved_input = dup(STDIN_FILENO);
        CHECK(saved_input >= 0 && dup2(input[0], STDIN_FILENO) == STDIN_FILENO);
        FIXTURE_CHECK(sync, rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_WAIT_CLOCK);
        FIXTURE_CHECK(sync,
                      dup2(saved_input, STDIN_FILENO) == STDIN_FILENO && close(saved_input) == 0);
        FIXTURE_CHECK(sync, fake.spawn_calls == 1 && fake.spawned_pid > 1);
        pid_t leader = 0, child = 0;
        /* Script startup may be delayed by macOS execution checks under load.
         * This fixture budget is independent of the production NTP deadline. */
        uint64_t deadline = real_monotonic() + 10000;
        while (real_monotonic() < deadline) {
            file = fopen(pids, "r");
            if (file) {
                long parent_value, child_value;
                int count = fscanf(file, "%ld %ld", &parent_value, &child_value);
                fclose(file);
                if (count == 2) {
                    leader = (pid_t)parent_value;
                    child = (pid_t)child_value;
                    break;
                }
            }
            pause_one_ms();
        }
        FIXTURE_CHECK(sync, leader == fake.spawned_pid && child > 1 && process_live(child));
        char retained[5];
        FIXTURE_CHECK(sync,
                      read(input[0], retained, sizeof(retained)) == (ssize_t)sizeof(retained));
        FIXTURE_CHECK(sync, !memcmp(retained, "keep\n", sizeof(retained)) && close(input[0]) == 0);
        if (exited_leader) {
            deadline = real_monotonic() + 2000;
            while (production->helper_exited(NULL, leader) == 0 && real_monotonic() < deadline) {
                pause_one_ms();
            }
            FIXTURE_CHECK(sync, production->helper_exited(NULL, leader) == 1);
            FIXTURE_CHECK(sync, rv1126b_time_sync_poll(sync, true) == RV1126B_TIME_FAILED);
        } else {
            FIXTURE_CHECK(sync, getpgid(leader) == leader && getpgid(child) == leader);
            FIXTURE_CHECK(sync, rv1126b_time_sync_poll(sync, false) == RV1126B_TIME_WAIT_NETWORK);
        }
        rv1126b_time_sync_destroy(sync);
        int status;
        CHECK(waitpid(leader, &status, WNOHANG) < 0 && errno == ECHILD);
        deadline = real_monotonic() + 2000;
        while (process_live(child) && real_monotonic() < deadline) {
            pause_one_ms();
        }
        CHECK(!process_live(child));
        CHECK(unlink(pids) == 0);
    }
    CHECK(unlink(helper) == 0 && rmdir(directory) == 0);
}

int main(void) {
    test_ready_and_network_gate();
    test_os_wait_deadline_and_recovery();
    test_helper_lifecycle_and_external_service();
    test_failures_and_configuration();
    test_service_process_names();
    test_real_owned_process_group();
    puts("Clock/network gate, monotonic deadline and owned time-helper lifecycle: PASS");
    return 0;
}
