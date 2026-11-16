/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "service_status.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int rv1126b_service_status(const char *state) {
    static char previous_path[PATH_MAX], previous_state[32];
    const char *path = getenv("MYBOT_STATUS_PATH");
    if (!path || !*path)
        return 0;
    if (!state || !*state || strlen(state) >= sizeof(previous_state) ||
        strspn(state, "abcdefghijklmnopqrstuvwxyz_") != strlen(state) || path[0] != '/' ||
        strlen(path) >= PATH_MAX)
        return -1;
    if (!strcmp(path, previous_path) && !strcmp(state, previous_state))
        return 0;
    char directory[PATH_MAX];
    strcpy(directory, path);
    char *name = strrchr(directory, '/');
    if (!name || name == directory || !name[1])
        return -1;
    *name++ = '\0';
    int parent = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat status;
    if (parent < 0)
        return -1;
    if (fstat(parent, &status) < 0 || status.st_uid != geteuid() ||
        (status.st_mode & 07777) != 0700) {
        close(parent);
        return -1;
    }
    int inspected = fstatat(parent, name, &status, AT_SYMLINK_NOFOLLOW);
    if ((inspected == 0 &&
         (!S_ISREG(status.st_mode) || status.st_uid != geteuid() || status.st_nlink != 1)) ||
        (inspected < 0 && errno != ENOENT)) {
        close(parent);
        return -1;
    }
    char temporary[64], contents[128];
    snprintf(temporary, sizeof(temporary), ".status-%ld", (long)getpid());
    int fd = openat(parent, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int count = snprintf(contents, sizeof(contents), "version=%s\npid=%ld\nstate=%s\n",
                         RV1126B_VERSION, (long)getpid(), state);
    int result = -1;
    if (fd >= 0) {
        size_t written = 0;
        while (written < (size_t)count) {
            ssize_t size = write(fd, contents + written, (size_t)count - written);
            if (size < 0 && errno == EINTR)
                continue;
            if (size <= 0)
                break;
            written += (size_t)size;
        }
        int closed = close(fd);
        if (written == (size_t)count && closed == 0) {
            if (renameat(parent, temporary, parent, name) == 0) {
                strcpy(previous_path, path);
                strcpy(previous_state, state);
                result = 0;
            }
        }
        unlinkat(parent, temporary, 0);
    }
    close(parent);
    return result;
}
