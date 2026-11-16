/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/volume.h"
#include "check.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char storage[] = "/tmp/mybot-volume-test-XXXXXX";
static char record[512];
static const mybot_audio_volume_ops_t *operations;

static void write_record(const char *text) {
    int fd = open(record, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(fchmod(fd, 0600) == 0);
    CHECK(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    CHECK(close(fd) == 0);
}

static void check_record(const char *expected) {
    int fd = open(record, O_RDONLY);
    CHECK(fd >= 0);
    char text[64] = {0};
    CHECK(read(fd, text, sizeof(text) - 1) == (ssize_t)strlen(expected));
    CHECK(!strcmp(text, expected));
    struct stat status;
    CHECK(fstat(fd, &status) == 0);
    CHECK((status.st_mode & 0777) == 0600);
    CHECK(status.st_uid == geteuid());
    CHECK(status.st_nlink == 1);
    CHECK(close(fd) == 0);
}

static void *attach(void) {
    void *context = NULL;
    CHECK(operations->init(&context) == 0);
    CHECK(context);
    return context;
}

static void check_level(void *context, int expected) {
    int level = -1;
    CHECK(operations->get_volume(context, &level) == 0);
    CHECK(level == expected);
    CHECK(rv1126b_volume_level() == expected);
}

static void test_lifecycle_and_pcm(void) {
    CHECK(rv1126b_volume_level() == 100);
    void *invalid = (void *)(uintptr_t)1;
    CHECK(operations->init(&invalid) < 0);
    CHECK(!invalid);
    CHECK(rv1126b_volume_prepare(NULL) < 0);
    CHECK(rv1126b_volume_prepare("") < 0);
    CHECK(rv1126b_volume_prepare(storage) == 0);
    CHECK(rv1126b_volume_prepare(storage) < 0);
    void *context = attach();
    CHECK(operations->init(NULL) < 0);
    CHECK(operations->init(&invalid) < 0);
    CHECK(!invalid);
    check_level(context, 100);
    CHECK(access(record, F_OK) < 0);
    CHECK(operations->set_volume(context, -1) < 0);
    CHECK(operations->set_volume(context, 101) < 0);
    CHECK(access(record, F_OK) < 0);
    CHECK(operations->get_volume(NULL, &(int){0}) < 0);
    CHECK(operations->get_volume(context, NULL) < 0);
    CHECK(operations->set_volume(NULL, 42) < 0);

    static const int16_t source[] = {-32768, -32767, -101, -100, -1, 0, 1, 100, 101, 32767};
    int16_t output[sizeof(source) / sizeof(source[0])];
    const size_t samples = sizeof(source) / sizeof(source[0]);
    rv1126b_volume_scale(output, source, samples);
    CHECK(!memcmp(source, output, sizeof(source)));
    CHECK(operations->set_volume(context, 50) == 0);
    const int16_t half[] = {-16384, -16384, -51, -50, -1, 0, 1, 50, 51, 16384};
    rv1126b_volume_scale(output, source, samples);
    CHECK(!memcmp(half, output, sizeof(half)));
    CHECK(source[0] == INT16_MIN && source[9] == INT16_MAX);
    memcpy(output, source, sizeof(source));
    rv1126b_volume_scale(output, output, samples);
    CHECK(!memcmp(half, output, sizeof(half)));
    rv1126b_volume_scale(NULL, source, samples);
    rv1126b_volume_scale(output, NULL, samples);
    rv1126b_volume_scale(output, source, 0);
    CHECK(!memcmp(half, output, sizeof(half)));
    check_record("50\n");

    struct stat before, after;
    CHECK(stat(record, &before) == 0);
    CHECK(operations->set_volume(context, 50) == 0);
    CHECK(stat(record, &after) == 0);
    CHECK(before.st_ino == after.st_ino);
    operations->destroy(context);
    CHECK(operations->get_volume(context, &(int){0}) < 0);
    CHECK(rv1126b_volume_level() == 50);
    context = attach();
    check_level(context, 50);
    CHECK(operations->set_volume(context, 0) == 0);
    rv1126b_volume_scale(output, source, samples);
    for (size_t i = 0; i < samples; ++i) {
        CHECK(output[i] == 0);
    }
    CHECK(operations->set_volume(context, 100) == 0);
    rv1126b_volume_scale(output, source, samples);
    CHECK(!memcmp(source, output, sizeof(source)));
    CHECK(operations->set_volume(context, 37) == 0);
    operations->destroy(context);
    rv1126b_volume_shutdown();
    CHECK(rv1126b_volume_level() == 100);

    /* A fresh process and a fresh product lifecycle both recover the level. */
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        CHECK(rv1126b_volume_prepare(storage) == 0);
        context = attach();
        check_level(context, 37);
        operations->destroy(context);
        rv1126b_volume_shutdown();
        _exit(0);
    }
    int child_status;
    CHECK(waitpid(child, &child_status, 0) == child);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    CHECK(rv1126b_volume_prepare(storage) == 0);
    context = attach();
    check_level(context, 37);
    operations->destroy(context);
    rv1126b_volume_shutdown();
}

static void test_corrupt_and_unsafe_storage(void) {
    const char *bad[] = {"",     "101\n", "-1\n", "50",    "50\nX",
                         "01\n", "a\n",   "1 \n", "1\n\n", "999999999999999999\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        write_record(bad[i]);
        CHECK(rv1126b_volume_prepare(storage) == 0);
        void *context = attach();
        check_level(context, 100);
        check_record(bad[i]);
        CHECK(operations->set_volume(context, 100) == 0);
        check_record("100\n");
        operations->destroy(context);
        rv1126b_volume_shutdown();
    }
    CHECK(chmod(record, 0644) == 0);
    CHECK(rv1126b_volume_prepare(storage) < 0);
    CHECK(rv1126b_volume_level() == 100);
    CHECK(chmod(record, 0600) == 0);
    CHECK(chmod(storage, 0755) == 0);
    CHECK(rv1126b_volume_prepare(storage) < 0);
    CHECK(chmod(storage, 0700) == 0);

    char other[512];
    CHECK(snprintf(other, sizeof(other), "%s/other", storage) > 0);
    CHECK(rename(record, other) == 0);
    CHECK(symlink(other, record) == 0);
    CHECK(rv1126b_volume_prepare(storage) < 0);
    CHECK(unlink(record) == 0);
    CHECK(link(other, record) == 0);
    CHECK(rv1126b_volume_prepare(storage) < 0);
    CHECK(unlink(record) == 0);
    CHECK(rename(other, record) == 0);

    char linked_directory[512];
    CHECK(snprintf(linked_directory, sizeof(linked_directory), "%s-link", storage) > 0);
    CHECK(symlink(storage, linked_directory) == 0);
    CHECK(rv1126b_volume_prepare(linked_directory) < 0);
    CHECK(unlink(linked_directory) == 0);

    CHECK(unlink(record) == 0);
    CHECK(mkfifo(record, 0600) == 0);
    CHECK(rv1126b_volume_prepare(storage) < 0);
    CHECK(unlink(record) == 0);
}

static void test_save_failure_keeps_gain(void) {
    CHECK(rv1126b_volume_prepare(storage) == 0);
    void *context = attach();
    CHECK(operations->set_volume(context, 64) == 0);
    CHECK(chmod(storage, 0500) == 0);
    CHECK(operations->set_volume(context, 25) < 0);
    check_level(context, 64);
    check_record("64\n");
    CHECK(chmod(storage, 0700) == 0);
    CHECK(operations->set_volume(context, 25) == 0);
    check_level(context, 25);
    CHECK(unlink(record) == 0);
    CHECK(symlink("untouched", record) == 0);
    CHECK(operations->set_volume(context, 12) < 0);
    check_level(context, 25);
    CHECK(unlink(record) == 0);
    CHECK(operations->set_volume(context, 12) == 0);
    check_record("12\n");
    operations->destroy(context);
    rv1126b_volume_shutdown();
}

typedef struct {
    void *context;
    bool finished;
    unsigned long reads;
} concurrent_t;

static void *read_levels(void *opaque) {
    concurrent_t *state = opaque;
    static const int16_t source[] = {INT16_MIN, INT16_MAX};
    do {
        int level = rv1126b_volume_level();
        CHECK(level >= 0 && level <= 100);
        int16_t output[2];
        rv1126b_volume_scale(output, source, 2);
        CHECK(output[0] <= 0);
        CHECK(output[1] >= 0);
        /* Both samples use one level snapshot, even during a saved update. */
        CHECK(output[0] + output[1] == 0 || output[0] + output[1] == -1);
        CHECK(operations->get_volume(state->context, &level) == 0);
        CHECK(level >= 0 && level <= 100);
        ++state->reads;
    } while (!__atomic_load_n(&state->finished, __ATOMIC_ACQUIRE));
    return NULL;
}

static void test_concurrent_playback_and_updates(void) {
    CHECK(rv1126b_volume_prepare(storage) == 0);
    concurrent_t state = {.context = attach()};
    pthread_t reader;
    CHECK(pthread_create(&reader, NULL, read_levels, &state) == 0);
    for (int i = 0; i < 20; ++i) {
        CHECK(operations->set_volume(state.context, i * 5) == 0);
    }
    __atomic_store_n(&state.finished, true, __ATOMIC_RELEASE);
    CHECK(pthread_join(reader, NULL) == 0);
    CHECK(state.reads > 0);
    check_level(state.context, 95);
    check_record("95\n");
    operations->destroy(state.context);
    rv1126b_volume_shutdown();
}

int main(void) {
    CHECK(mkdtemp(storage));
    CHECK(snprintf(record, sizeof(record), "%s/volume.settings", storage) > 0);
    operations = rv1126b_volume_ops();
    test_lifecycle_and_pcm();
    test_corrupt_and_unsafe_storage();
    test_save_failure_keeps_gain();
    test_concurrent_playback_and_updates();
    CHECK(unlink(record) == 0);
    CHECK(rmdir(storage) == 0);
    puts("Persistent volume lifecycle, PCM gain, private storage and concurrent playback: PASS");
    return 0;
}
