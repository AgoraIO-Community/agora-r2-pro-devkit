/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "wifi_backend_internal.h"
#include "../network_route.h"
#include "system_wifi_profile.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WIFI_STATUS_LIMIT 4096
#define WIFI_ROUTE_LIMIT 65536
#define WIFI_COMMAND_TIMEOUT 3000
static const char *const program_names[] = {"ip",    "iw", "hostapd", "dnsmasq", "wpa_supplicant",
                                            "udhcpc"};
typedef struct {
    char *config[6];
    bool initialized, owns_supplicant, concurrent, virtual_ap, ap_address, ap_ready;
    bool dhcp_started, dhcp_owned, commit_pending;
    int storage_fd, runtime_fd;
    char interface_name[IFNAMSIZ], ap_interface[IFNAMSIZ], ssid[WIFI_SSID_CAPACITY];
    /* Runtime sockets require at most 72 bytes; extra space lets initialization
     * reject an oversized template before creating any directory. */
    char runtime[80], trial[9];
    char commit_status[WIFI_STATUS_LIMIT], commit_route[WIFI_ROUTE_LIMIT], commit_generation[129];
    char programs[6][PATH_MAX];
    wifi_scan_t cached_scan;
    wifi_child_t hostapd, dnsmasq, supplicant, dhcp;
    wifi_control_t control;
} linux_backend_t;
enum {
    CONFIG_INTERFACE,
    CONFIG_STORAGE,
    CONFIG_RUNTIME,
    CONFIG_SCRIPT,
    CONFIG_SYSTEM,
    CONFIG_LEASES
};
static bool initialize(void *context, char *error);
static bool attach_supplicant(linux_backend_t *backend);
static void stop_ap(void *context);
static bool ap_running(void *context);
static void stop_dhcp(linux_backend_t *backend);
static void remove_trial(linux_backend_t *backend);
static bool query(linux_backend_t *backend, const char *command, unsigned timeout, char *output,
                  size_t capacity) {
    wifi_buffer_t response;
    wifi_buffer_init(&response, 65536);
    bool ok = wifi_control_request(&backend->control, command, timeout, &response) &&
              wifi_copy(output, capacity, response.data ? response.data : "");
    if (!ok && capacity)
        output[0] = 0;
    wifi_buffer_free(&response);
    return ok;
}

static bool ping(linux_backend_t *backend, unsigned timeout) {
    char response[64];
    return query(backend, "PING", timeout, response, sizeof(response)) &&
           !strncmp(response, "PONG", 4);
}

static bool run(linux_backend_t *backend, const char *const *arguments, wifi_buffer_t *output,
                char *error) {
    const char *resolved[32];
    size_t count = 0;
    while (arguments && arguments[count] && count < 31) {
        resolved[count] = arguments[count];
        ++count;
    }
    if (!count || arguments[count]) {
        wifi_error(error, "Missing required Wi-Fi executable");
        return false;
    }
    resolved[count] = NULL;
    size_t program = 0;
    for (; program < 6; ++program)
        if (!strcmp(arguments[0], program_names[program]))
            break;
    if (program == 6 || !backend->programs[program][0]) {
        wifi_error(error, "Missing required Wi-Fi executable");
        return false;
    }
    resolved[0] = backend->programs[program];
    return wifi_command(resolved, WIFI_COMMAND_TIMEOUT, NULL, output, error);
}

static bool run_discard(linux_backend_t *backend, const char *const *arguments, char *error) {
    wifi_buffer_t output;
    wifi_buffer_init(&output, WIFI_COMMAND_OUTPUT_LIMIT);
    bool ok = run(backend, arguments, &output, error);
    wifi_buffer_free(&output);
    return ok;
}

static bool has_address(const char *interface_name, const char *expected) {
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses))
        return false;
    bool found = false;
    for (struct ifaddrs *item = addresses; item; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            strcmp(interface_name, item->ifa_name))
            continue;
        char text[INET_ADDRSTRLEN];
        const struct sockaddr_in *address = (const struct sockaddr_in *)item->ifa_addr;
        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) && !strcmp(text, expected)) {
            found = true;
            break;
        }
    }
    freeifaddrs(addresses);
    return found;
}

static bool online(void *context) {
#ifdef __linux__
    linux_backend_t *backend = context;
    return backend->interface_name[0] &&
           rv1126b_network_route_ready(backend->interface_name, 0xc0a80401U) > 0;
#else
    (void)context;
    return false;
#endif
}

static void routed_fingerprint(linux_backend_t *backend, char *output, size_t capacity) {
    output[0] = 0;
    if (!online(backend))
        return;
    wifi_buffer_t result;
    wifi_buffer_init(&result, capacity - 1);
    struct ifaddrs *addresses = NULL;
    bool ok = !getifaddrs(&addresses);
    if (ok) {
        for (struct ifaddrs *item = addresses; item && ok; item = item->ifa_next)
            if (item->ifa_addr && item->ifa_addr->sa_family == AF_INET &&
                !strcmp(item->ifa_name, backend->interface_name))
                ok = wifi_buffer_printf(
                    &result, "%u;", ((const struct sockaddr_in *)item->ifa_addr)->sin_addr.s_addr);
        freeifaddrs(addresses);
    }
    FILE *routes = fopen("/proc/net/route", "r");
    char line[1024];
    size_t length = strlen(backend->interface_name);
    if (!routes)
        ok = false;
    while (routes && ok && fgets(line, sizeof(line), routes))
        if (!strncmp(line, backend->interface_name, length) &&
            (line[length] == '\t' || line[length] == ' ')) {
            line[strcspn(line, "\n")] = 0;
            ok = wifi_buffer_text(&result, line);
        }
    if (routes)
        fclose(routes);
    if (ok)
        wifi_copy(output, capacity, result.data ? result.data : "");
    wifi_buffer_free(&result);
}

static bool lease_matches(linux_backend_t *backend, const char *previous, const char *status) {
    wifi_dhcpcd_lease_t lease;
    if (!wifi_read_dhcpcd_lease(backend->config[CONFIG_LEASES], backend->interface_name, &lease))
        return false;
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses))
        return false;
    /* Check each actual IPv4 without imposing a limit on address aliases. */
    bool match = false;
    for (struct ifaddrs *item = addresses; item && !match; item = item->ifa_next) {
        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            strcmp(item->ifa_name, backend->interface_name))
            continue;
        char text[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &((const struct sockaddr_in *)item->ifa_addr)->sin_addr, text,
                      sizeof(text))) {
            const char *values[] = {text};
            match = wifi_dhcpcd_lease_matches(&lease, previous, status, values, 1);
        }
    }
    freeifaddrs(addresses);
    return match;
}

static void discard_initialization(linux_backend_t *backend) {
    wifi_control_close(&backend->control);
    if (backend->storage_fd >= 0)
        close(backend->storage_fd);
    backend->storage_fd = -1;
    if (backend->runtime_fd >= 0)
        close(backend->runtime_fd);
    backend->runtime_fd = -1;
    if (backend->runtime[0])
        rmdir(backend->runtime);
    backend->runtime[0] = 0;
}

static bool initialize(void *context, char *error) {
#ifndef __linux__
    (void)discard_initialization;
    (void)context;
    wifi_error(error, "Wi-Fi AP provisioning requires Linux nl80211");
    return false;
#else
    linux_backend_t *backend = context;
    if (backend->initialized)
        return true;
    if ((*backend->config[CONFIG_SYSTEM] && *backend->config[CONFIG_SYSTEM] != '/') ||
        (*backend->config[CONFIG_LEASES] && *backend->config[CONFIG_LEASES] != '/')) {
        wifi_error(error, "System Wi-Fi profile and lease paths must be absolute");
        return false;
    }
    wifi_copy(backend->interface_name, sizeof(backend->interface_name),
              backend->config[CONFIG_INTERFACE]);
    if (!*backend->config[CONFIG_INTERFACE]) {
        DIR *directory = opendir("/sys/class/net");
        struct dirent *entry;
        while (directory && (entry = readdir(directory))) {
            char path[PATH_MAX];
            struct stat st;
            snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", entry->d_name);
            if (!wifi_valid_interface(entry->d_name) || stat(path, &st))
                continue;
            if (!backend->interface_name[0] || strcmp(entry->d_name, "wlan0") == 0 ||
                (strcmp(backend->interface_name, "wlan0") &&
                 strcmp(entry->d_name, backend->interface_name) < 0))
                wifi_copy(backend->interface_name, sizeof(backend->interface_name), entry->d_name);
        }
        if (directory)
            closedir(directory);
    }
    char path[PATH_MAX];
    struct stat st;
    snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", backend->interface_name);
    if (!wifi_valid_interface(backend->config[CONFIG_INTERFACE][0]
                                  ? backend->config[CONFIG_INTERFACE]
                                  : backend->interface_name) ||
        stat(path, &st)) {
        wifi_error(error, "No nl80211 Wi-Fi interface; configure the Wi-Fi interface explicitly");
        return false;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/addr_assign_type", backend->interface_name);
    wifi_buffer_t mac;
    wifi_buffer_init(&mac, 256);
    const bool permanent_mac = wifi_read_text(path, &mac) &&
                               (mac.length == 1 || (mac.length == 2 && mac.data[1] == '\n')) &&
                               mac.data[0] == '0';
    wifi_buffer_free(&mac);
    if (!permanent_mac) {
        wifi_error(error, "Wi-Fi interface does not report a permanent MAC address");
        return false;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", backend->interface_name);
    wifi_buffer_init(&mac, 256);
    const bool valid_mac =
        wifi_read_text(path, &mac) &&
        wifi_hotspot_ssid_from_mac(mac.data, backend->ssid, sizeof(backend->ssid));
    wifi_buffer_free(&mac);
    if (!valid_mac) {
        wifi_error(error, "Cannot read Wi-Fi MAC address");
        return false;
    }
    for (size_t i = 0; i < 6; ++i)
        if (!wifi_find_program(program_names[i], backend->programs[i],
                               sizeof(backend->programs[i]))) {
            char message[WIFI_ERROR_CAPACITY];
            snprintf(message, sizeof(message), "Missing required Wi-Fi tool: %s", program_names[i]);
            wifi_error(error, message);
            return false;
        }
    const char *script = backend->config[CONFIG_SCRIPT];
    if (*script != '/' || lstat(script, &st) || !S_ISREG(st.st_mode) || (st.st_mode & 0022) ||
        access(script, X_OK)) {
        wifi_error(error, "Missing or unsafe executable Wi-Fi DHCP hook");
        return false;
    }
    int base = -1;
    if (!wifi_private_directory(backend->config[CONFIG_STORAGE], &base, error))
        return false;
    close(base);
    int used = snprintf(path, sizeof(path), "%s/wifi", backend->config[CONFIG_STORAGE]);
    if (used < 0 || (size_t)used >= sizeof(path) ||
        !wifi_private_directory(path, &backend->storage_fd, error))
        return false;
    const char *runtime_base = *backend->config[CONFIG_RUNTIME] ? backend->config[CONFIG_RUNTIME]
                                                                : "/run/mybot-rv1126b-wifi";
    if (!wifi_private_directory(runtime_base, &base, error)) {
        discard_initialization(backend);
        return false;
    }
    close(base);
    used = snprintf(backend->runtime, sizeof(backend->runtime), "%s/session-XXXXXX", runtime_base);
    if (used < 0 || (size_t)used >= sizeof(backend->runtime) || !mkdtemp(backend->runtime)) {
        backend->runtime[0] = 0;
        wifi_error(error, "Cannot create private Wi-Fi runtime session");
        discard_initialization(backend);
        return false;
    }
    if (strlen(backend->runtime) > 72 ||
        !wifi_private_directory(backend->runtime, &backend->runtime_fd, error)) {
        wifi_error(error, "Wi-Fi runtime path is unsafe or too long for Unix control sockets");
        discard_initialization(backend);
        return false;
    }
    backend->initialized = true;
    if (*backend->config[CONFIG_SYSTEM]) {
        wifi_credentials_t profiles;
        if (!attach_supplicant(backend)) {
            wifi_error(error, "System Wi-Fi mode requires the existing supplicant control socket");
            backend->initialized = false;
            discard_initialization(backend);
            return false;
        }
        if (!wifi_read_system_profiles(backend->config[CONFIG_SYSTEM], &profiles, error)) {
            backend->initialized = false;
            discard_initialization(backend);
            return false;
        }
    }
    return true;
#endif
}

static bool attach_supplicant(linux_backend_t *backend) {
    static const char *const bases[] = {"/run/wpa_supplicant/", "/var/run/wpa_supplicant/"};
    char remote[PATH_MAX], local[PATH_MAX];
    snprintf(local, sizeof(local), "%s/wpa-client", backend->runtime);
    for (size_t i = 0; i < 2; ++i) {
        snprintf(remote, sizeof(remote), "%s%s", bases[i], backend->interface_name);
        if (wifi_control_open(&backend->control, remote, local) && ping(backend, 200))
            return true;
    }
    wifi_control_close(&backend->control);
    return false;
}

static bool ensure_supplicant(linux_backend_t *backend, char *error) {
    if (ping(backend, 200))
        return true;
    if (!backend->owns_supplicant && attach_supplicant(backend))
        return true;
    if (*backend->config[CONFIG_SYSTEM]) {
        wifi_error(
            error,
            "System Wi-Fi mode never starts a competing supplicant; restore its control socket");
        return false;
    }
    if (!wifi_child_running(&backend->supplicant)) {
        if (!backend->owns_supplicant &&
            wifi_process_uses_interface("wpa_supplicant", backend->interface_name)) {
            wifi_error(error, "Existing supplicant has no accessible control socket; hand off "
                              "Wi-Fi management or expose its per-interface control socket");
            return false;
        }
        backend->owns_supplicant = true;
        char configuration[PATH_MAX + 64], path[PATH_MAX];
        snprintf(configuration, sizeof(configuration), "ctrl_interface=%s/wpa\nupdate_config=0\n",
                 backend->runtime);
        snprintf(path, sizeof(path), "%s/wpa.conf", backend->runtime);
        const char *arguments[] = {
            backend->programs[4], "-Dnl80211", "-i", backend->interface_name, "-c", path, NULL};
        if (!wifi_atomic_private_file(backend->runtime_fd, "wpa.conf", configuration, error) ||
            !wifi_child_start(&backend->supplicant, arguments, -1, NULL, error))
            return false;
    }
    uint64_t deadline = wifi_now_ms() + 3000;
    char remote[PATH_MAX], local[PATH_MAX];
    snprintf(remote, sizeof(remote), "%s/wpa/%s", backend->runtime, backend->interface_name);
    snprintf(local, sizeof(local), "%s/wpa-client", backend->runtime);
    while (wifi_child_running(&backend->supplicant) && wifi_now_ms() < deadline) {
        if (wifi_control_open(&backend->control, remote, local) && ping(backend, 100))
            return true;
        wifi_sleep_ms(50);
    }
    wifi_error(error, "Owned wpa_supplicant did not expose a working control socket");
    return false;
}

static void remove_trial(linux_backend_t *backend) {
    if (!backend->trial[0])
        return;
    char command[64];
    snprintf(command, sizeof(command), "REMOVE_NETWORK %s", backend->trial);
    wifi_control_ok(&backend->control, command);
    wifi_control_ok(&backend->control, "REASSOCIATE");
    backend->trial[0] = 0;
}

static void dhcp_environment(linux_backend_t *backend, char *runtime, size_t runtime_capacity,
                             char *interface_name, size_t interface_capacity) {
    snprintf(runtime, runtime_capacity, "MYBOT_WIFI_RUNTIME=%s", backend->runtime);
    snprintf(interface_name, interface_capacity, "MYBOT_WIFI_INTERFACE=%s",
             backend->interface_name);
}

static bool start_dhcp(linux_backend_t *backend, char *error) {
    char runtime[PATH_MAX + 32], interface_name[IFNAMSIZ + 32], pidfile[PATH_MAX];
    dhcp_environment(backend, runtime, sizeof(runtime), interface_name, sizeof(interface_name));
    const char *environment[] = {runtime, interface_name, NULL};
    snprintf(pidfile, sizeof(pidfile), "%s/udhcpc.pid", backend->runtime);
    const char *arguments[] = {backend->programs[5],
                               "-f",
                               "-R",
                               "-i",
                               backend->interface_name,
                               "-s",
                               backend->config[CONFIG_SCRIPT],
                               "-p",
                               pidfile,
                               "-t",
                               "3",
                               "-T",
                               "3",
                               "-A",
                               "2",
                               NULL};
    backend->dhcp_owned = wifi_child_start(&backend->dhcp, arguments, -1, environment, error);
    return backend->dhcp_owned;
}

static void stop_dhcp(linux_backend_t *backend) {
    wifi_child_stop(&backend->dhcp);
    if (backend->dhcp_owned && backend->runtime[0]) {
        char runtime[PATH_MAX + 32], interface_name[IFNAMSIZ + 32], error[WIFI_ERROR_CAPACITY];
        dhcp_environment(backend, runtime, sizeof(runtime), interface_name, sizeof(interface_name));
        const char *environment[] = {runtime, interface_name, NULL};
        const char *arguments[] = {backend->config[CONFIG_SCRIPT], "mybot-cleanup", NULL};
        wifi_child_t cleanup;
        wifi_child_init(&cleanup);
        if (wifi_child_start(&cleanup, arguments, -1, environment, error)) {
            uint64_t deadline = wifi_now_ms() + 500;
            while (wifi_child_running(&cleanup) && wifi_now_ms() < deadline)
                wifi_sleep_ms(10);
        }
        wifi_child_stop(&cleanup);
    }
    backend->dhcp_started = false;
    backend->dhcp_owned = false;
}

static const char *ap_ssid(void *context) {
    return ((linux_backend_t *)context)->ssid;
}

static bool concurrent_ap(void *context) {
    return ((linux_backend_t *)context)->concurrent;
}

static bool ap_running(void *context) {
    linux_backend_t *backend = context;
    return backend->ap_ready && wifi_child_running(&backend->hostapd) &&
           wifi_child_running(&backend->dnsmasq);
}

static bool start_ap(void *context, char *error) {
    linux_backend_t *backend = context;
    if (!backend->initialized && !initialize(backend, error))
        return false;
    if (ap_running(backend))
        return true;
    stop_ap(backend);
    if (!ping(backend, 100) && !backend->owns_supplicant)
        attach_supplicant(backend);
    char response[65536];
    if (ping(backend, 100) && query(backend, "SCAN_RESULTS", 200, response, sizeof(response)))
        wifi_parse_scan_results(response, &backend->cached_scan);
    snprintf(backend->ap_interface, sizeof(backend->ap_interface), "mbap%ld",
             (long)getpid() % 1000000);
    const char *add_ap[] = {"iw",        "dev",  backend->interface_name,
                            "interface", "add",  backend->ap_interface,
                            "type",      "__ap", NULL};
    if (!if_nametoindex(backend->ap_interface) && run_discard(backend, add_ap, error)) {
        backend->virtual_ap = true;
        backend->concurrent = true;
    } else {
        backend->ap_interface[0] = 0;
        backend->concurrent = false;
        if (!backend->owns_supplicant &&
            (attach_supplicant(backend) ||
             wifi_process_uses_interface("wpa_supplicant", backend->interface_name) ||
             wifi_process_uses_interface("hostapd", backend->interface_name))) {
            wifi_error(error,
                       "Driver cannot create an AP+STA interface; existing Wi-Fi manager owns the "
                       "station. Provide AP+STA support or hand off this interface explicitly");
            return false;
        }
        stop_dhcp(backend);
        remove_trial(backend);
        wifi_control_close(&backend->control);
        wifi_child_stop(&backend->supplicant);
        backend->owns_supplicant = true;
        wifi_copy(backend->ap_interface, sizeof(backend->ap_interface), backend->interface_name);
    }
    char frequency[32];
    int station_frequency = 0;
    if (query(backend, "STATUS", 200, response, sizeof(response)) &&
        wifi_field(response, "freq", frequency, sizeof(frequency)))
        station_frequency = atoi(frequency);
    bool multiple = false;
    if (backend->concurrent && station_frequency >= 5000) {
        wifi_buffer_t output;
        wifi_buffer_init(&output, WIFI_COMMAND_OUTPUT_LIMIT);
        char phy[16], inspection_error[WIFI_ERROR_CAPACITY];
        const char *info[] = {"iw", "dev", backend->interface_name, "info", NULL};
        if (run(backend, info, &output, inspection_error) &&
            wifi_wiphy_from_interface_info(output.data ? output.data : "", phy, sizeof(phy))) {
            const char *capabilities[] = {"iw", "phy", phy, "info", NULL};
            if (run(backend, capabilities, &output, inspection_error))
                multiple = wifi_managed_ap_multiple_channels(output.data ? output.data : "");
        }
        wifi_buffer_free(&output);
    }
    wifi_ap_radio_plan_t radio;
    if (!wifi_select_ap_radio(station_frequency, multiple, &radio, error)) {
        stop_ap(backend);
        return false;
    }
    char configuration[PATH_MAX + 512], encoded[65];
    wifi_encode_hex(backend->ssid, strlen(backend->ssid), encoded, sizeof(encoded));
    snprintf(configuration, sizeof(configuration),
             "interface=%s\ndriver=nl80211\nssid2=%s\nhw_mode=%c\nchannel=%d\nmax_num_sta=4\nauth_"
             "algs=1\nwpa=0\nap_isolate=1\nctrl_interface=%s/hostapd\n",
             backend->ap_interface, encoded, radio.hw_mode, radio.channel, backend->runtime);
    const char *link_up[] = {"ip", "link", "set", "dev", backend->ap_interface, "up", NULL};
    if (!wifi_atomic_private_file(backend->runtime_fd, "hostapd.conf", configuration, error) ||
        !run_discard(backend, link_up, error)) {
        stop_ap(backend);
        return false;
    }
    if (has_address(backend->ap_interface, "192.168.4.1")) {
        wifi_error(error, "Provisioning address 192.168.4.1 is already owned on the AP interface");
        stop_ap(backend);
        return false;
    }
    const char *address_add[] = {
        "ip", "address", "add", "192.168.4.1/24", "dev", backend->ap_interface, NULL};
    if (!run_discard(backend, address_add, error)) {
        stop_ap(backend);
        return false;
    }
    backend->ap_address = true;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/hostapd.conf", backend->runtime);
    const char *hostapd[] = {backend->programs[2], path, NULL};
    if (!wifi_child_start(&backend->hostapd, hostapd, -1, NULL, error)) {
        stop_ap(backend);
        return false;
    }
    bool enabled = false;
    uint64_t deadline = wifi_now_ms() + 4000;
    while (wifi_child_running(&backend->hostapd) && wifi_now_ms() < deadline) {
        wifi_control_t control;
        wifi_control_init(&control);
        char remote[PATH_MAX], local[PATH_MAX];
        snprintf(remote, sizeof(remote), "%s/hostapd/%s", backend->runtime, backend->ap_interface);
        snprintf(local, sizeof(local), "%s/ap-client", backend->runtime);
        wifi_buffer_t status;
        wifi_buffer_init(&status, 65536);
        if (wifi_control_open(&control, remote, local) &&
            wifi_control_request(&control, "STATUS", 200, &status)) {
            char state[64];
            enabled = wifi_field(status.data ? status.data : "", "state", state, sizeof(state)) &&
                      !strcmp(state, "ENABLED");
        }
        wifi_control_close(&control);
        wifi_buffer_free(&status);
        if (enabled)
            break;
        wifi_sleep_ms(50);
    }
    if (!enabled) {
        wifi_error(error,
                   "hostapd did not enable the AP (check driver, regulatory domain and channel)");
        stop_ap(backend);
        return false;
    }
    char interface_option[64], leasefile[PATH_MAX + 32], pidfile[PATH_MAX + 32];
    snprintf(interface_option, sizeof(interface_option), "--interface=%s", backend->ap_interface);
    snprintf(leasefile, sizeof(leasefile), "--dhcp-leasefile=%s/leases", backend->runtime);
    snprintf(pidfile, sizeof(pidfile), "--pid-file=%s/dnsmasq.pid", backend->runtime);
    const char *dhcp[] = {backend->programs[3],
                          "--keep-in-foreground",
                          "--conf-file=/dev/null",
                          "--port=0",
                          interface_option,
                          "--bind-interfaces",
                          "--except-interface=lo",
                          "--dhcp-range=192.168.4.2,192.168.4.5,255.255.255.0,10m",
                          "--dhcp-option=3,192.168.4.1",
                          "--dhcp-option=6,192.168.4.1",
                          "--dhcp-lease-max=4",
                          leasefile,
                          pidfile,
                          geteuid() == 0 ? "--user=root" : NULL,
                          NULL};
    if (!wifi_child_start(&backend->dnsmasq, dhcp, -1, NULL, error)) {
        stop_ap(backend);
        return false;
    }
    wifi_sleep_ms(200);
    if (!wifi_child_running(&backend->hostapd) || !wifi_child_running(&backend->dnsmasq)) {
        wifi_error(error,
                   "AP DHCP service failed to start; check port 67 ownership and privileges");
        stop_ap(backend);
        return false;
    }
    backend->ap_ready = true;
    wifi_error(error, "");
    return true;
}

static void stop_ap(void *context) {
    linux_backend_t *backend = context;
    backend->ap_ready = false;
    wifi_child_stop(&backend->dnsmasq);
    wifi_child_stop(&backend->hostapd);
    char error[WIFI_ERROR_CAPACITY];
    if (backend->ap_address && backend->ap_interface[0]) {
        const char *remove_address[] = {
            "ip", "address", "del", "192.168.4.1/24", "dev", backend->ap_interface, NULL};
        run_discard(backend, remove_address, error);
    }
    backend->ap_address = false;
    if (backend->virtual_ap && backend->ap_interface[0]) {
        const char *remove_interface[] = {"iw", "dev", backend->ap_interface, "del", NULL};
        run_discard(backend, remove_interface, error);
    }
    backend->virtual_ap = false;
    backend->ap_interface[0] = 0;
    if (backend->runtime_fd >= 0) {
        unlinkat(backend->runtime_fd, "hostapd.conf", 0);
        unlinkat(backend->runtime_fd, "leases", 0);
        unlinkat(backend->runtime_fd, "dnsmasq.pid", 0);
    }
}

static bool scan(void *context, wifi_scan_t *result, char *error) {
    linux_backend_t *backend = context;
    if (!backend->initialized && !initialize(backend, error))
        return false;
    if (!backend->concurrent && ap_running(backend)) {
        *result = backend->cached_scan;
        wifi_error(error, result->count
                              ? ""
                              : "Active scanning is unavailable while this single-interface AP is "
                                "running; enter a hidden SSID manually");
        return result->count != 0;
    }
    if (!ensure_supplicant(backend, error))
        return false;
    char response[65536];
    if (!query(backend, "SCAN", 1000, response, sizeof(response)) ||
        (strncmp(response, "OK", 2) && !strstr(response, "FAIL-BUSY"))) {
        wifi_error(error, "Wi-Fi driver rejected scanning");
        return false;
    }
    wifi_sleep_ms(250);
    if (!query(backend, "SCAN_RESULTS", 1000, response, sizeof(response)) ||
        strncmp(response, "bssid", 5)) {
        wifi_error(error, "Cannot read Wi-Fi scan results");
        return false;
    }
    wifi_parse_scan_results(response, &backend->cached_scan);
    *result = backend->cached_scan;
    wifi_error(error, "");
    return true;
}

static bool load(void *context, wifi_credentials_t *result, char *error) {
    linux_backend_t *backend = context;
    if (!backend->initialized && !initialize(backend, error))
        return false;
    bool ok = *backend->config[CONFIG_SYSTEM]
                  ? wifi_read_system_profiles(backend->config[CONFIG_SYSTEM], result, error)
                  : wifi_read_credentials(backend->storage_fd, result, error);
    if (ok)
        wifi_error(error, "");
    return ok;
}

static bool reconfigure(void *context) {
    linux_backend_t *backend = context;
    /* RECONFIGURE can reuse IDs; an old trial must never remove a committed profile. */
    backend->trial[0] = 0;
    wifi_control_close(&backend->control);
    char run_remote[PATH_MAX], var_remote[PATH_MAX], local[PATH_MAX];
    snprintf(run_remote, sizeof(run_remote), "/run/wpa_supplicant/%s", backend->interface_name);
    snprintf(var_remote, sizeof(var_remote), "/var/run/wpa_supplicant/%s", backend->interface_name);
    snprintf(local, sizeof(local), "%s/wpa-client", backend->runtime);
    const char *remotes[] = {run_remote, var_remote, NULL};
    return wifi_reconfigure_control(remotes, local, 3000) && attach_supplicant(backend);
}

static bool save(void *context, const wifi_credentials_t *profiles, char *error) {
    linux_backend_t *backend = context;
    backend->commit_pending = false;
    if (!backend->initialized && !initialize(backend, error))
        return false;
    if (*backend->config[CONFIG_SYSTEM]) {
        if (backend->owns_supplicant || !ensure_supplicant(backend, error)) {
            wifi_error(error, "System Wi-Fi persistence requires its external supplicant");
            return false;
        }
        query(backend, "STATUS", 200, backend->commit_status, sizeof(backend->commit_status));
        routed_fingerprint(backend, backend->commit_route, sizeof(backend->commit_route));
        wifi_dhcpcd_lease_t lease;
        backend->commit_generation[0] = 0;
        if (wifi_read_dhcpcd_lease(backend->config[CONFIG_LEASES], backend->interface_name, &lease))
            wifi_copy(backend->commit_generation, sizeof(backend->commit_generation),
                      lease.generation);
        backend->commit_pending = wifi_commit_system_profiles(
            backend->config[CONFIG_SYSTEM], profiles, reconfigure, backend, error);
    } else
        backend->commit_pending = wifi_write_credentials(backend->storage_fd, profiles, error);
    return backend->commit_pending;
}

static bool status_associated(const char *status, const char *ssid, const char *id) {
    char state[64], selected_id[32], name[256], decoded[WIFI_SSID_CAPACITY];
    return *id && wifi_field(status, "wpa_state", state, sizeof(state)) &&
           !strcmp(state, "COMPLETED") &&
           wifi_field(status, "id", selected_id, sizeof(selected_id)) && !strcmp(selected_id, id) &&
           wifi_field(status, "ssid", name, sizeof(name)) &&
           wifi_decode_wpa(name, decoded, sizeof(decoded)) && !strcmp(decoded, ssid);
}

typedef struct {
    linux_backend_t *backend;
    const wifi_cancel_t *cancel;
} selection_context_t;
static bool selection_send(void *context, const char *command) {
    selection_context_t *selection = context;
    return !wifi_cancelled(selection->cancel) &&
           wifi_control_ok(&selection->backend->control, command);
}

static bool wait_committed(void *context, const wifi_credential_t *credential, unsigned timeout_ms,
                           const wifi_cancel_t *cancel, char *error) {
    linux_backend_t *backend = context;
    if (!backend->commit_pending) {
        wifi_error(error, "No committed Wi-Fi update is pending verification");
        return false;
    }
    if (!*backend->config[CONFIG_SYSTEM]) {
        bool ready = !wifi_cancelled(cancel) && online(backend);
        backend->commit_pending = false;
        wifi_error(error, ready ? "" : "Committed Wi-Fi connection is no longer routed");
        return ready;
    }
    uint64_t deadline = wifi_now_ms() + (timeout_ms < 120000 ? timeout_ms : 120000),
             selection_grace = wifi_now_ms() + 500;
    uint64_t stable_since = 0, lease_wait_since = 0;
    char stable_identity[WIFI_ROUTE_LIMIT + 128] = "", lease_wait_identity[128] = "";
    bool uninterrupted = true, selection_issued = false, renewal_issued = false;
    bool last_associated = false, last_routed = false, last_lease = false;
    while (!wifi_cancelled(cancel) && wifi_now_ms() < deadline) {
        char status[WIFI_STATUS_LIMIT], networks[65536], loaded_id[9] = "", route[WIFI_ROUTE_LIMIT];
        if (!query(backend, "STATUS", 200, status, sizeof(status)) && attach_supplicant(backend))
            query(backend, "STATUS", 200, status, sizeof(status));
        query(backend, "LIST_NETWORKS", 200, networks, sizeof(networks));
        wifi_network_id_for_ssid(networks, credential->ssid, loaded_id);
        bool associated = status_associated(status, credential->ssid, loaded_id);
        routed_fingerprint(backend, route, sizeof(route));
        if (!associated || !*route)
            uninterrupted = false;
        if (!associated && !selection_issued && wifi_now_ms() >= selection_grace) {
            selection_context_t selection = {backend, cancel};
            if (!wifi_ensure_committed_selection(status, networks, credential->ssid, selection_send,
                                                 &selection, &selection_issued, error)) {
                backend->commit_pending = false;
                return false;
            }
        }
        bool fresh_lease = lease_matches(backend, backend->commit_generation, status);
        bool retained_lease = uninterrupted && wifi_same_association_lease(
                                                   backend->commit_status, status, credential->ssid,
                                                   loaded_id, backend->commit_route, route);
        bool ready = associated && *route && (fresh_lease || retained_lease);
        char bssid[64] = "", identity[WIFI_ROUTE_LIMIT + 128];
        wifi_field(status, "bssid", bssid, sizeof(bssid));
        snprintf(identity, sizeof(identity), "%s\n%s\n%s", bssid, loaded_id, route);
        last_associated = associated;
        last_routed = *route != 0;
        last_lease = fresh_lease || retained_lease;
        if (associated && !ready && !renewal_issued) {
            char association[128];
            snprintf(association, sizeof(association), "%s\n%s", bssid, loaded_id);
            if (!lease_wait_since || strcmp(lease_wait_identity, association)) {
                lease_wait_since = wifi_now_ms();
                wifi_copy(lease_wait_identity, sizeof(lease_wait_identity), association);
            }
            if (wifi_now_ms() - lease_wait_since >= 1000) {
                renewal_issued = true;
                char manager[PATH_MAX];
                if (wifi_find_program("dhcpcd", manager, sizeof(manager)) &&
                    wifi_process_uses_interface("dhcpcd", backend->interface_name)) {
                    uint64_t now = wifi_now_ms();
                    if (now < deadline) {
                        unsigned remaining = (unsigned)(deadline - now);
                        const char *arguments[] = {manager, "-n", backend->interface_name, NULL};
                        wifi_buffer_t output;
                        wifi_buffer_init(&output, WIFI_COMMAND_OUTPUT_LIMIT);
                        char refresh_error[WIFI_ERROR_CAPACITY];
                        wifi_command(arguments,
                                     remaining < WIFI_COMMAND_TIMEOUT ? remaining
                                                                      : WIFI_COMMAND_TIMEOUT,
                                     cancel, &output, refresh_error);
                        wifi_buffer_free(&output);
                    }
                }
            }
        } else if (!associated) {
            lease_wait_since = 0;
            lease_wait_identity[0] = 0;
        }
        if (ready) {
            if (!stable_since || strcmp(stable_identity, identity)) {
                stable_since = wifi_now_ms();
                wifi_copy(stable_identity, sizeof(stable_identity), identity);
            }
            if (wifi_now_ms() - stable_since >= 1000) {
                backend->commit_pending = false;
                wifi_error(error, "");
                return true;
            }
        } else {
            stable_since = 0;
            stable_identity[0] = 0;
        }
        wifi_sleep_ms(100);
    }
    backend->commit_pending = false;
    wifi_error(error, wifi_cancelled(cancel) ? "Wi-Fi operation cancelled"
                      : !last_associated     ? "Saved Wi-Fi profile did not complete association"
                      : !last_routed         ? "Saved Wi-Fi profile has no routed IPv4 lease"
                      : !last_lease ? "Saved Wi-Fi profile has no current DHCP lease confirmation"
                                    : "Saved Wi-Fi profile did not regain a stable routed lease");
    return false;
}

static bool set_network(linux_backend_t *backend, const char *number, const char *field,
                        const char *value) {
    char command[512];
    int used = snprintf(command, sizeof(command), "SET_NETWORK %s %s %s", number, field, value);
    return used >= 0 && (size_t)used < sizeof(command) &&
           wifi_control_ok(&backend->control, command);
}

static bool restore_priority(linux_backend_t *backend, bool persisted, const char *number,
                             const char *priority) {
    return !persisted || set_network(backend, number, "priority", priority);
}

static bool numeric_id(const char *number) {
    if (!*number || strlen(number) > 8)
        return false;
    for (const unsigned char *p = (const unsigned char *)number; *p; ++p)
        if (!isdigit(*p))
            return false;
    return true;
}

static bool connect_network(void *context, const wifi_credential_t *credential, unsigned timeout_ms,
                            const wifi_cancel_t *cancel, char *error) {
    linux_backend_t *backend = context;
    if (!memchr(credential->ssid, 0, sizeof(credential->ssid)) ||
        !memchr(credential->psk, 0, sizeof(credential->psk)) ||
        !wifi_valid_ssid(credential->ssid) ||
        (*credential->psk && !wifi_hex_psk(credential->psk))) {
        wifi_error(error, "Invalid Wi-Fi credentials");
        return false;
    }
    if (wifi_cancelled(cancel)) {
        wifi_error(error, "Wi-Fi operation cancelled");
        return false;
    }
    if (!backend->initialized && !initialize(backend, error))
        return false;
    char initial_status[WIFI_STATUS_LIMIT], original_route[WIFI_ROUTE_LIMIT];
    if (!query(backend, "STATUS", 200, initial_status, sizeof(initial_status)) &&
        !backend->owns_supplicant && attach_supplicant(backend))
        query(backend, "STATUS", 200, initial_status, sizeof(initial_status));
    routed_fingerprint(backend, original_route, sizeof(original_route));
    wifi_dhcpcd_lease_t initial_lease;
    bool had_initial = wifi_read_dhcpcd_lease(backend->config[CONFIG_LEASES],
                                              backend->interface_name, &initial_lease);
    if (!backend->concurrent && ap_running(backend))
        stop_ap(backend);
    stop_dhcp(backend);
    remove_trial(backend);
    if (!ensure_supplicant(backend, error))
        return false;
    char number[32] = "";
    bool persisted = false;
    if (*backend->config[CONFIG_SYSTEM]) {
        wifi_credentials_t profiles;
        if (!wifi_read_system_profiles(backend->config[CONFIG_SYSTEM], &profiles, error))
            return false;
        for (size_t i = 0; i < profiles.count; ++i)
            persisted |= !strcmp(profiles.items[i].ssid, credential->ssid) &&
                         !strcmp(profiles.items[i].psk, credential->psk);
        if (persisted) {
            char networks[65536];
            query(backend, "LIST_NETWORKS", 1000, networks, sizeof(networks));
            wifi_network_id_for_ssid(networks, credential->ssid, number);
            if (!*number) {
                char response[64];
                if (query(backend, "RECONFIGURE", 1000, response, sizeof(response)) &&
                    !strncmp(response, "OK", 2)) {
                    query(backend, "LIST_NETWORKS", 1000, networks, sizeof(networks));
                    wifi_network_id_for_ssid(networks, credential->ssid, number);
                }
            }
            if (!*number) {
                wifi_error(error, "Persisted system Wi-Fi profile is not loaded by its supplicant");
                return false;
            }
        }
    }
    if (!persisted)
        query(backend, "ADD_NETWORK", 1000, number, sizeof(number));
    size_t number_length = strlen(number);
    if (number_length && number[number_length - 1] == '\n')
        number[number_length - 1] = 0;
    if (!numeric_id(number)) {
        wifi_error(error, "Cannot create temporary supplicant network");
        return false;
    }
    wifi_copy(backend->trial, sizeof(backend->trial), persisted ? "" : number);
    char original_priority[64] = "";
    if (persisted) {
        char command[64];
        snprintf(command, sizeof(command), "GET_NETWORK %s priority", number);
        query(backend, command, 1000, original_priority, sizeof(original_priority));
        size_t priority_length = strlen(original_priority);
        if (priority_length && original_priority[priority_length - 1] == '\n')
            original_priority[priority_length - 1] = 0;
        char *end = NULL;
        errno = 0;
        long priority = strtol(original_priority, &end, 10);
        if (!*original_priority || errno || !end || *end || priority < INT_MIN ||
            priority > INT_MAX) {
            wifi_error(error,
                       "Cannot inspect persisted network priority without changing system policy");
            return false;
        }
    }
    char encoded[65];
    wifi_encode_hex(credential->ssid, strlen(credential->ssid), encoded, sizeof(encoded));
    bool configured =
        set_network(backend, number, "ssid", encoded) &&
        set_network(backend, number, "scan_ssid", "1") &&
        set_network(backend, number, "priority", "2147483647") &&
        set_network(backend, number, "key_mgmt", *credential->psk ? "WPA-PSK" : "NONE");
    if (configured && *credential->psk)
        configured = set_network(backend, number, "psk", credential->psk);
    char enable[64];
    snprintf(enable, sizeof(enable), "ENABLE_NETWORK %s", number);
    if (!configured || !wifi_control_ok(&backend->control, enable) ||
        !wifi_control_ok(&backend->control, "REASSOCIATE")) {
        wifi_error(error, "Supplicant rejected temporary network configuration");
        restore_priority(backend, persisted, number, original_priority);
        remove_trial(backend);
        return false;
    }
    uint64_t deadline = wifi_now_ms() + (timeout_ms < 120000 ? timeout_ms : 120000);
    bool associated = false;
    while (!wifi_cancelled(cancel) && wifi_now_ms() < deadline) {
        char current_status[WIFI_STATUS_LIMIT];
        query(backend, "STATUS", 200, current_status, sizeof(current_status));
        associated = status_associated(current_status, credential->ssid, number);
        if (associated) {
            if (!backend->dhcp_started) {
                if (!wifi_process_uses_interface("udhcpc", backend->interface_name) &&
                    !wifi_process_uses_interface("dhcpcd", backend->interface_name) &&
                    !wifi_process_uses_interface("dhclient", backend->interface_name)) {
                    if (*backend->config[CONFIG_SYSTEM]) {
                        wifi_error(
                            error,
                            "System Wi-Fi mode requires its existing DHCP manager and lease hook");
                        restore_priority(backend, persisted, number, original_priority);
                        remove_trial(backend);
                        return false;
                    }
                    if (!start_dhcp(backend, error)) {
                        restore_priority(backend, persisted, number, original_priority);
                        remove_trial(backend);
                        return false;
                    }
                }
                backend->dhcp_started = true;
            }
            char current_route[WIFI_ROUTE_LIMIT], lease_ready[PATH_MAX];
            routed_fingerprint(backend, current_route, sizeof(current_route));
            bool event_lease =
                lease_matches(backend, had_initial ? initial_lease.generation : "", current_status);
            snprintf(lease_ready, sizeof(lease_ready), "%s/lease.ready", backend->runtime);
            bool fresh_lease = backend->dhcp_owned ? access(lease_ready, R_OK) == 0
                               : *backend->config[CONFIG_SYSTEM]
                                   ? event_lease
                                   : event_lease || strcmp(current_route, original_route);
            bool same_lease =
                !backend->dhcp_owned &&
                wifi_same_association_lease(initial_status, current_status, credential->ssid,
                                            number, original_route, current_route);
            if (online(backend) && (fresh_lease || same_lease)) {
                if (!restore_priority(backend, persisted, number, original_priority)) {
                    wifi_error(error, "Cannot restore persisted network priority");
                    return false;
                }
                wifi_error(error, "");
                return true;
            }
        }
        wifi_sleep_ms(100);
    }
    wifi_error(error, wifi_cancelled(cancel) ? "Wi-Fi operation cancelled"
                      : associated
                          ? "Wi-Fi associated but no fresh routed IPv4 lease was obtained; hand "
                            "off the DHCP manager if its previous lease is unchanged"
                          : "Wi-Fi connection timed out; verify SSID and password");
    restore_priority(backend, persisted, number, original_priority);
    stop_dhcp(backend);
    remove_trial(backend);
    return false;
}

static void destroy(void *context) {
    linux_backend_t *backend = context;
    if (!backend)
        return;
    stop_ap(backend);
    stop_dhcp(backend);
    remove_trial(backend);
    wifi_child_stop(&backend->supplicant);
    wifi_control_close(&backend->control);
    if (backend->storage_fd >= 0)
        close(backend->storage_fd);
    if (backend->runtime_fd >= 0) {
        int duplicate = dup(backend->runtime_fd);
        DIR *directory = duplicate >= 0 ? fdopendir(duplicate) : NULL;
        if (directory) {
            struct dirent *entry;
            while ((entry = readdir(directory)))
                if (entry->d_name[0] != '.')
                    unlinkat(backend->runtime_fd, entry->d_name, 0);
            closedir(directory);
        } else if (duplicate >= 0)
            close(duplicate);
        unlinkat(backend->runtime_fd, "wpa", AT_REMOVEDIR);
        unlinkat(backend->runtime_fd, "hostapd", AT_REMOVEDIR);
        close(backend->runtime_fd);
        rmdir(backend->runtime);
    }
    for (size_t i = 0; i < 6; ++i)
        free(backend->config[i]);
    free(backend);
}

static const wifi_backend_ops_t operations = {
    initialize, ap_ssid, start_ap, stop_ap,        ap_running,      concurrent_ap, online,
    scan,       load,    save,     wait_committed, connect_network, destroy};
bool wifi_linux_backend_create(const wifi_backend_config_t *config, wifi_backend_t *result,
                               char *error) {
    if (!config || !result) {
        wifi_error(error, "Invalid Wi-Fi backend configuration");
        return false;
    }
    result->ops = NULL;
    result->context = NULL;
    linux_backend_t *backend = calloc(1, sizeof(*backend));
    if (!backend) {
        wifi_error(error, "Cannot allocate Wi-Fi backend");
        return false;
    }
    backend->storage_fd = backend->runtime_fd = -1;
    backend->concurrent = true;
    wifi_child_init(&backend->hostapd);
    wifi_child_init(&backend->dnsmasq);
    wifi_child_init(&backend->supplicant);
    wifi_child_init(&backend->dhcp);
    wifi_control_init(&backend->control);
    const char *system = config->system_wpa_config && *config->system_wpa_config
                             ? config->system_wpa_config
                             : getenv("MYBOT_SYSTEM_WPA_CONFIG");
    const char *leases = config->dhcpcd_lease_directory && *config->dhcpcd_lease_directory
                             ? config->dhcpcd_lease_directory
                             : getenv("MYBOT_DHCPCD_LEASE_DIR");
    if (system && *system && (!leases || !*leases))
        leases = "/run/mybot-network/leases";
    const char *values[] = {config->interface_name,
                            config->storage_directory,
                            config->runtime_directory,
                            config->dhcp_script,
                            system,
                            leases};
    for (size_t i = 0; i < 6; ++i) {
        backend->config[i] = strdup(values[i] ? values[i] : "");
        if (!backend->config[i]) {
            wifi_error(error, "Cannot copy Wi-Fi backend configuration");
            destroy(backend);
            return false;
        }
    }
    result->ops = &operations;
    result->context = backend;
    wifi_error(error, "");
    return true;
}

void wifi_backend_destroy(wifi_backend_t *backend) {
    if (!backend)
        return;
    if (backend->ops && backend->ops->destroy)
        backend->ops->destroy(backend->context);
    backend->ops = NULL;
    backend->context = NULL;
}
