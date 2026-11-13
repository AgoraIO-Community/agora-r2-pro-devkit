/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "runtime_guard.h"
#include "check.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char temporary[] = "/tmp/mybot-guard-XXXXXX";
    CHECK(mkdtemp(temporary));
    char lock_path[PATH_MAX], link_path[PATH_MAX], private_path[PATH_MAX];
    CHECK(snprintf(lock_path, sizeof(lock_path), "%s/instance.lock", temporary) > 0);
    CHECK(snprintf(link_path, sizeof(link_path), "%s/symlink", temporary) > 0);
    CHECK(snprintf(private_path, sizeof(private_path), "%s/private", temporary) > 0);
    CHECK(rv1126b_runtime_lock(lock_path, false) == -2);
    int fd = rv1126b_runtime_lock(lock_path, true);
    CHECK(fd >= 0 && rv1126b_runtime_lock(lock_path, true) == -1);
    CHECK(symlink(lock_path, link_path) == 0);
    CHECK(rv1126b_runtime_lock(link_path, true) == -1);
    CHECK(close(fd) == 0);
    fd = rv1126b_runtime_lock(lock_path, false);
    CHECK(fd >= 0 && close(fd) == 0);
    CHECK(rv1126b_prepare_storage("relative") < 0 && rv1126b_prepare_storage("/") < 0);
    int cwd = open(".", O_RDONLY);
    CHECK(cwd >= 0 && rv1126b_prepare_storage(private_path) == 0);
    CHECK(fchdir(cwd) == 0);
    CHECK(chmod(private_path, 0755) == 0 && rv1126b_prepare_storage(private_path) < 0);
    CHECK(close(cwd) == 0);
    CHECK(unlink(link_path) == 0 && unlink(lock_path) == 0);
    CHECK(rmdir(private_path) == 0 && rmdir(temporary) == 0);
    puts("Single instance, lock symlinks and private storage permissions: PASS");
    return 0;
}
