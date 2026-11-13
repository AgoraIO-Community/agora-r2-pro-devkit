/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "runtime_guard.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int rv1126b_runtime_lock(const char *path, bool create) {
    int fd = open(path, (create ? O_CREAT | O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return !create && errno == ENOENT ? -2 : -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || (create && st.st_uid != geteuid()) ||
        flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int rv1126b_legacy_service_running(void) {
    DIR *processes = opendir("/proc");
    if (!processes)
        return -1;
    int running = 0;
    struct dirent *entry;
    while ((entry = readdir(processes)) != NULL) {
        if (!entry->d_name[0] || strspn(entry->d_name, "0123456789") != strlen(entry->d_name))
            continue;
        char path[512], name[64];
        if (snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name) >= (int)sizeof(path))
            continue;
        FILE *file = fopen(path, "r");
        if (!file)
            continue;
        bool read = fgets(name, sizeof(name), file) != NULL;
        fclose(file);
        if (read && (!strcmp(name, "botstation-devi\n") || !strcmp(name, "botstation-device\n"))) {
            running = 1;
            break;
        }
    }
    closedir(processes);
    return running;
}

int rv1126b_prepare_storage(const char *path) {
    if (path[0] != '/' || !path[1])
        return -1;
    if (!strncmp(path, "/userdata/", 10)) {
        FILE *mounts = fopen("/proc/mounts", "r");
        if (!mounts)
            return -1;
        char line[512], target[128];
        bool mounted = false;
        while (fgets(line, sizeof(line), mounts)) {
            if (sscanf(line, "%*s %127s", target) == 1 && !strcmp(target, "/userdata"))
                mounted = true;
        }
        int error = ferror(mounts);
        if (fclose(mounts) || error || !mounted) {
            fprintf(stderr, "/userdata must be mounted before private storage is created\n");
            return -1;
        }
    }
    if (mkdir(path, 0700) < 0 && errno != EEXIST)
        return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct stat st;
    int result =
        fstat(fd, &st) < 0 || st.st_uid != geteuid() || (st.st_mode & 0077) || fchdir(fd) < 0 ? -1
                                                                                              : 0;
    close(fd);
    return result;
}
