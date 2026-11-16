/* SPDX-License-Identifier: Apache-2.0 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "service_status.h"
#include "check.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void check_status(const char *path, const char *state) {
    FILE *file = fopen(path, "r");
    CHECK(file);
    char text[256] = {0};
    CHECK(fread(text, 1, sizeof(text) - 1, file) > 0);
    CHECK(fclose(file) == 0);
    CHECK(strstr(text, "version="));
    CHECK(strstr(text, state));
    struct stat status;
    CHECK(stat(path, &status) == 0 && (status.st_mode & 0777) == 0600);
}

int main(void) {
    CHECK(unsetenv("MYBOT_STATUS_PATH") == 0);
    CHECK(rv1126b_service_status("starting") == 0);
    char directory[] = "/tmp/mybot-status-test-XXXXXX";
    CHECK(mkdtemp(directory));
    char path[512], other[512];
    CHECK(snprintf(path, sizeof(path), "%s/application.status", directory) > 0);
    CHECK(snprintf(other, sizeof(other), "%s/other", directory) > 0);
    CHECK(setenv("MYBOT_STATUS_PATH", path, 1) == 0);
    CHECK(rv1126b_service_status("ready") == 0);
    check_status(path, "state=ready\n");
    CHECK(rv1126b_service_status("ready") == 0);
    CHECK(rv1126b_service_status("conversation") == 0);
    check_status(path, "state=conversation\n");
    CHECK(rv1126b_service_status("ready\nsecret=x") < 0);
    CHECK(rv1126b_service_status(NULL) < 0);
    CHECK(chmod(directory, 0755) == 0);
    CHECK(rv1126b_service_status("offline") < 0);
    CHECK(chmod(directory, 0700) == 0);
    CHECK(rename(path, other) == 0);
    CHECK(symlink(other, path) == 0);
    CHECK(rv1126b_service_status("offline") < 0);
    CHECK(unlink(path) == 0);
    CHECK(rv1126b_service_status("stopped") == 0);
    check_status(path, "state=stopped\n");
    CHECK(unlink(path) == 0 && unlink(other) == 0 && rmdir(directory) == 0);
    puts("Private atomic application status and unsafe-path rejection: PASS");
    return 0;
}
