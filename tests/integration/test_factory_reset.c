/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "check.h"
#include "factory_reset_internal.h"
#include "system_wifi_profile.h"
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static char config[PATH_MAX];

typedef struct {
    int socket;
    bool reject_reload;
    bool nonempty_list;
    bool still_associated;
    bool saw_empty_file;
    bool disconnected;
} station_t;

static void write_file(const char *path, const char *contents) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    CHECK(fd >= 0 && write(fd, contents, strlen(contents)) == (ssize_t)strlen(contents));
    CHECK(!close(fd));
}

static bool exists(const char *path) {
    struct stat info;
    if (!lstat(path, &info))
        return true;
    CHECK(errno == ENOENT);
    return false;
}

static void read_file(const char *path, char *output, size_t size) {
    FILE *file = fopen(path, "r");
    CHECK(file);
    size_t bytes = fread(output, 1, size - 1, file);
    CHECK(!ferror(file) && !fclose(file) && bytes < size - 1);
    output[bytes] = 0;
}

static void *serve(void *context) {
    station_t *station = context;
    for (;;) {
        struct sockaddr_un sender;
        socklen_t length = sizeof(sender);
        char command[80] = {0};
        ssize_t bytes = recvfrom(station->socket, command, sizeof(command) - 1, 0,
                                 (struct sockaddr *)&sender, &length);
        CHECK(bytes > 0);
        const char *reply = "FAIL\n";
        if (!strcmp(command, "QUIT"))
            break;
        if (!strcmp(command, "PING"))
            reply = "PONG\n";
        if (!strcmp(command, "RECONFIGURE")) {
            char text[4096];
            read_file(config, text, sizeof(text));
            if (!strstr(text, "network=") && !strstr(text, "network ="))
                station->saw_empty_file = true;
            reply = station->reject_reload ? "FAIL\n" : "OK\n";
        }
        if (!strcmp(command, "LIST_NETWORKS"))
            reply = station->nonempty_list ? "network id / ssid / bssid / flags\n0\tsaved\tany\t\n"
                                           : "network id / ssid / bssid / flags\n";
        if (!strcmp(command, "DISCONNECT")) {
            station->disconnected = true;
            reply = "OK\n";
        }
        if (!strcmp(command, "STATUS"))
            reply = station->disconnected && !station->still_associated ? "wpa_state=DISCONNECTED\n"
                                                                        : "wpa_state=COMPLETED\n";
        CHECK(sendto(station->socket, reply, strlen(reply), 0, (struct sockaddr *)&sender,
                     length) == (ssize_t)strlen(reply));
    }
    return NULL;
}

static void run_with_station(factory_reset_paths_t *paths, bool reject, bool remain,
                             bool still_associated, bool expect_ok) {
    station_t station = {.socket = socket(AF_UNIX, SOCK_DGRAM, 0),
                         .reject_reload = reject,
                         .nonempty_list = remain,
                         .still_associated = still_associated};
    CHECK(station.socket >= 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    CHECK(strlen(paths->remote_socket) < sizeof(addr.sun_path));
    strcpy(addr.sun_path, paths->remote_socket);
    CHECK(!bind(station.socket, (struct sockaddr *)&addr, sizeof(addr)));
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, serve, &station));
    CHECK((factory_reset_run(paths, "wlan0") == 0) == expect_ok);
    int stop = socket(AF_UNIX, SOCK_DGRAM, 0);
    CHECK(stop >= 0 && sendto(stop, "QUIT", 4, 0, (struct sockaddr *)&addr, sizeof(addr)) == 4);
    CHECK(!pthread_join(thread, NULL));
    CHECK(station.saw_empty_file);
    CHECK(station.disconnected == (expect_ok || still_associated));
    CHECK(!close(stop) && !close(station.socket) && !unlink(paths->remote_socket));
}

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *walk) {
    (void)info;
    (void)type;
    (void)walk;
    return remove(path);
}

int main(void) {
    char temporary[] = "/tmp/reset-XXXXXX", base[PATH_MAX];
    CHECK(mkdtemp(temporary) && realpath(temporary, base) && !chdir(base));
    CHECK(!mkdir("state", 0700) && !mkdir("data", 0700) && !mkdir("data/wifi", 0700) &&
          !mkdir("leases", 0700) && !mkdir("etc", 0700));
    char state[PATH_MAX], data[PATH_MAX], leases[PATH_MAX], remote[PATH_MAX], local[PATH_MAX];
    CHECK(realpath("state", state) && realpath("data", data) && realpath("leases", leases));
    CHECK(snprintf(remote, sizeof(remote), "%s/wpa", base) < (int)sizeof(remote));
    CHECK(snprintf(local, sizeof(local), "%s/control", base) < (int)sizeof(local));
    write_file("etc/wpa.conf", "country=CN\nnetwork={\n ssid=\"fixture\"\n key_mgmt=NONE\n}\n");
    CHECK(realpath("etc/wpa.conf", config));
    factory_reset_paths_t paths = {.state = state,
                                   .data = data,
                                   .leases = leases,
                                   .config = config,
                                   .remote_socket = remote,
                                   .local_socket = local,
                                   .app_lock = "app-lock"};
    write_file("data/device_auth", "local credential");
    write_file("data/volume.settings", "saved volume");
    write_file("data/wifi/wifi.credentials", "old private store");
    write_file("leases/wlan0.lease", "lease");
    write_file("leases/usb0.lease", "usb lease");
    write_file("data/unrelated.log", "retain");
    write_file("state/backup", "retain");
    CHECK(factory_reset_run(&paths, "wlan0") == 0 && !exists("app-lock"));
    write_file("state/factory-reset.pending", "bad");
    CHECK(factory_reset_run(&paths, "wlan0") < 0 && !exists("app-lock"));
    write_file("state/factory-reset.pending", "");
    CHECK(factory_reset_run(&paths, "usb0") < 0 && !exists("app-lock"));

    char before[4096], after[4096];
    read_file(config, before, sizeof(before));
    int lock = open(paths.app_lock, O_RDWR | O_CREAT, 0600);
    CHECK(lock >= 0 && !flock(lock, LOCK_EX | LOCK_NB));
    CHECK(factory_reset_run(&paths, NULL) < 0 && !close(lock));
    CHECK(factory_reset_run(&paths, NULL) < 0 && exists("state/factory-reset.pending"));
    read_file(config, after, sizeof(after));
    CHECK(!strcmp(before, after));
    CHECK(!rename("data/device_auth", "untouched") && !symlink("../untouched", "data/device_auth"));
    CHECK(factory_reset_run(&paths, NULL) < 0);
    CHECK(!unlink("data/device_auth") && !rename("untouched", "data/device_auth"));
    run_with_station(&paths, true, false, false, false);
    read_file(config, after, sizeof(after));
    CHECK(!strcmp(before, after) && exists("data/device_auth"));
    run_with_station(&paths, false, true, false, false);
    run_with_station(&paths, false, false, true, false);
    CHECK(exists("state/factory-reset.pending") && !exists("state/factory-reset.setup") &&
          exists("data/device_auth") && exists("leases/wlan0.lease"));

    /* Resume with a partially cleared KV store and an already durable setup marker. */
    CHECK(!unlink("data/volume.settings"));
    write_file("state/factory-reset.setup", "");
    run_with_station(&paths, false, false, false, true);
    CHECK(!exists("data/device_auth") && !exists("data/volume.settings") &&
          !exists("data/wifi/wifi.credentials") && !exists("leases/wlan0.lease") &&
          !exists("state/factory-reset.pending") && exists("state/factory-reset.setup"));
    CHECK(exists("data/unrelated.log") && exists("state/backup") && exists("leases/usb0.lease"));
    read_file(config, after, sizeof(after));
    CHECK(strstr(after, "country=CN") && !strstr(after, "network={"));
    CHECK(factory_reset_run(&paths, NULL) == 0);
    int statefd = factory_private_directory(state);
    CHECK(statefd >= 0 && factory_file_status(statefd, FACTORY_SETUP_FILE, true) == 1);
    CHECK(!factory_remove_file(statefd, FACTORY_SETUP_FILE, true) && !close(statefd));
    CHECK(!chdir("/") && !nftw(base, remove_entry, 8, FTW_DEPTH | FTW_PHYS));
    puts("Factory reset transaction, crash recovery and real supplicant protocol fixture: PASS");
    return 0;
}
