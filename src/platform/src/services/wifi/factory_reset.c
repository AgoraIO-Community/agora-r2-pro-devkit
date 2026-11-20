/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "factory_reset_internal.h"
#include "system_wifi_profile.h"
#include "wifi_backend_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static bool reload(void *context) {
    const factory_reset_paths_t *paths = context;
    const char *remotes[] = {paths->remote_socket, NULL};
    return wifi_reconfigure_control(remotes, paths->local_socket, 3000);
}

static bool disconnected(const char *status) {
    char value[128];
    if (!wifi_field(status, "wpa_state", value, sizeof(value)) ||
        (strcmp(value, "DISCONNECTED") && strcmp(value, "INACTIVE") &&
         strcmp(value, "INTERFACE_DISABLED")))
        return false;
    return !wifi_field(status, "bssid", value, sizeof(value)) &&
           !wifi_field(status, "ssid", value, sizeof(value));
}

static bool clear_network(const factory_reset_paths_t *paths) {
    struct stat endpoint;
    if (lstat(paths->remote_socket, &endpoint) < 0 || !S_ISSOCK(endpoint.st_mode) ||
        endpoint.st_uid != geteuid())
        return false;
    /* A helper interrupted during the last attempt can leave its own bound
     * socket in the private service runtime. The application lock serializes cleanup. */
    if (lstat(paths->local_socket, &endpoint) == 0) {
        if (!S_ISSOCK(endpoint.st_mode) || endpoint.st_uid != geteuid() ||
            unlink(paths->local_socket) < 0)
            return false;
    } else if (errno != ENOENT) {
        return false;
    }
    wifi_credentials_t profiles = {0};
    char error[WIFI_ERROR_CAPACITY] = "";
    if (!wifi_commit_system_profiles(paths->config, &profiles, reload, (void *)paths, error) ||
        !wifi_read_system_profiles(paths->config, &profiles, error) || profiles.count)
        return false;

    wifi_control_t control;
    wifi_control_init(&control);
    wifi_buffer_t response;
    wifi_buffer_init(&response, 65536);
    bool ok = wifi_control_open(&control, paths->remote_socket, paths->local_socket) &&
              wifi_control_request(&control, "LIST_NETWORKS", 1000, &response) &&
              !strcmp(response.data, "network id / ssid / bssid / flags\n") &&
              wifi_control_ok(&control, "DISCONNECT");
    bool verified = false;
    uint64_t deadline = wifi_now_ms() + 3000;
    while (ok && wifi_now_ms() < deadline) {
        if (wifi_control_request(&control, "STATUS", 250, &response) &&
            disconnected(response.data)) {
            verified = true;
            break;
        }
        wifi_sleep_ms(50);
    }
    wifi_buffer_free(&response);
    wifi_control_close(&control);
    return verified;
}

static int application_lock(const char *path) {
    int fd = open(path, O_CREAT | O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    struct stat info;
    if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_uid != geteuid() || (info.st_mode & 07777) != 0600 ||
        flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* A missing file or directory can mean the device has never used that store
 * or a prior reset already cleared it. Any other access failure blocks reset. */
static bool optional_directory(const char *path, int *fd) {
    *fd = factory_private_directory(path);
    return *fd >= 0 || errno == ENOENT;
}

static bool clear_files(int state, const factory_reset_paths_t *paths) {
    int data = -1, wifi = -1, leases = -1;
    char legacy_wifi[PATH_MAX];
    const char *stage = "private state validation";
    bool ok = false;
    if (snprintf(legacy_wifi, sizeof(legacy_wifi), "%s/wifi", paths->data) >=
            (int)sizeof(legacy_wifi) ||
        !optional_directory(paths->data, &data) || !optional_directory(legacy_wifi, &wifi) ||
        !optional_directory(paths->leases, &leases) ||
        factory_file_status(state, FACTORY_SETUP_FILE, true) < 0 ||
        (data >= 0 && (factory_file_status(data, "device_auth", false) < 0 ||
                       factory_file_status(data, "volume.settings", false) < 0)) ||
        (wifi >= 0 && factory_file_status(wifi, "wifi.credentials", false) < 0) ||
        (leases >= 0 && factory_file_status(leases, FACTORY_STATION ".lease", false) < 0))
        goto done;
    stage = "station profile reload or disconnect";
    if (!clear_network(paths))
        goto done;
    stage = "private settings removal";
    if ((leases >= 0 && factory_remove_file(leases, "wlan0.lease", false) < 0) ||
        (wifi >= 0 && factory_remove_file(wifi, "wifi.credentials", false) < 0) ||
        (data >= 0 && (factory_remove_file(data, "device_auth", false) < 0 ||
                       factory_remove_file(data, "volume.settings", false) < 0)))
        goto done;
    stage = "setup marker commit";
    /* Persist the forced setup state before retiring the pending intent. If
     * power is lost between the commits the pending transaction can run again. */
    ok = factory_create_marker(state, FACTORY_SETUP_FILE) == 0 &&
         factory_remove_file(state, FACTORY_PENDING_FILE, true) == 0;
done:
    if (!ok)
        fprintf(stderr, "Factory reset failed during %s\n", stage);
    if (leases >= 0)
        close(leases);
    if (wifi >= 0)
        close(wifi);
    if (data >= 0)
        close(data);
    return ok;
}

int factory_reset_run(const factory_reset_paths_t *paths, const char *station) {
    int state = factory_private_directory(paths->state);
    if (state < 0)
        return -1;
    int pending = factory_file_status(state, FACTORY_PENDING_FILE, true);
    if (pending <= 0) {
        close(state);
        return pending;
    }
    if (!factory_station_supported(station)) {
        fprintf(stderr, "Factory reset requires the board station wlan0\n");
        close(state);
        return -1;
    }
    int lock = application_lock(paths->app_lock);
    if (lock < 0) {
        fprintf(stderr, "Factory reset cannot acquire the application lock\n");
        close(state);
        return -1;
    }
    /* Multiple starts can observe pending before either one owns the lock. */
    pending = factory_file_status(state, FACTORY_PENDING_FILE, true);
    int result = pending > 0 ? (clear_files(state, paths) ? 0 : -1) : pending;
    close(lock);
    close(state);
    return result;
}
