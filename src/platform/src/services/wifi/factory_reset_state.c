/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "factory_reset_internal.h"
#include "mybot_platform/factory_reset.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int factory_private_directory(const char *path) {
    if (!path || path[0] != '/' || !path[1] || strlen(path) >= PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    char copy[PATH_MAX];
    strcpy(copy, path + 1);
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char *component = copy;
    while (fd >= 0) {
        char *slash = strchr(component, '/');
        if (slash)
            *slash = '\0';
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) {
            close(fd);
            errno = EINVAL;
            return -1;
        }
        int next = openat(fd, component, O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        int saved = errno;
        close(fd);
        if (next < 0) {
            errno = saved;
            return -1;
        }
        fd = next;
        if (!slash)
            break;
        component = slash + 1;
    }
    if (fd < 0)
        return -1;
    struct stat info;
    if (fstat(fd, &info) < 0 || info.st_uid != geteuid() || (!geteuid() && info.st_gid != 0) ||
        (info.st_mode & 07777) != 0700) {
        close(fd);
        errno = EPERM;
        return -1;
    }
    return fd;
}

int factory_file_status(int directory, const char *name, bool marker) {
    struct stat info;
    if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1 ||
        (info.st_mode & 07777) != 0600 ||
        (marker && (info.st_size != 0 || (!geteuid() && info.st_gid != 0)))) {
        errno = EPERM;
        return -1;
    }
    return 1;
}

int factory_create_marker(int directory, const char *name) {
    int exists = factory_file_status(directory, name, true);
    if (exists < 0)
        return -1;
    int flags = O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    int fd = openat(directory, name, exists ? flags : flags | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;
    int result = fsync(fd);
    if (close(fd) < 0)
        result = -1;
    return result < 0 ? -1 : fsync(directory);
}

int factory_remove_file(int directory, const char *name, bool marker) {
    int exists = factory_file_status(directory, name, marker);
    if (exists < 0 || (exists && unlinkat(directory, name, 0) < 0))
        return -1;
    /* A retry after an interrupted unlink must also sync the parent. */
    return fsync(directory) == 0 && factory_file_status(directory, name, marker) == 0 ? 0 : -1;
}

int rv1126b_factory_setup_pending(void) {
    int state = factory_private_directory(FACTORY_STATE_DIRECTORY);
    if (state < 0)
        return errno == ENOENT ? 0 : -1;
    int status = factory_file_status(state, FACTORY_SETUP_FILE, true);
    close(state);
    return status;
}

int rv1126b_factory_setup_complete(void) {
    int state = factory_private_directory(FACTORY_STATE_DIRECTORY);
    if (state < 0)
        return errno == ENOENT ? 0 : -1;
    int result = factory_remove_file(state, FACTORY_SETUP_FILE, true);
    close(state);
    return result;
}
