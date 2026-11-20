/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "check.h"
#include "wifi_backend_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void hotspot_names(void) {
    static const struct {
        const char *mac;
        const char *ssid;
    } vectors[] = {{"AA:BB:CC:00:00:01", "mybot-5639"},
                   {"AA:BB:CC:00:00:02", "mybot-f36c"},
                   {"02:11:22:33:44:55", "mybot-fc1d"},
                   {"00:11:22:33:44:55", "mybot-bc46"},
                   {"02:AA:BB:CC:DD:EE", "mybot-65e0"}};
    char output[WIFI_SSID_CAPACITY];
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        CHECK(wifi_hotspot_ssid_from_mac(vectors[i].mac, output, sizeof(output)));
        CHECK(!strcmp(output, vectors[i].ssid) && strlen(output) == 10);
        CHECK(strspn(output + 6, "0123456789abcdef") == 4);
    }
    CHECK(wifi_hotspot_ssid_from_mac("aa:bb:cc:00:00:01", output, sizeof(output)));
    CHECK(!strcmp(output, vectors[0].ssid));
    CHECK(wifi_hotspot_ssid_from_mac("aA:bB:cC:00:00:01\n", output, sizeof(output)));
    CHECK(!strcmp(output, vectors[0].ssid));
    CHECK(wifi_hotspot_ssid_from_mac("AA:BB:CC:00:00:01\r\n", output, sizeof(output)));
    CHECK(!strcmp(output, vectors[0].ssid));
    const char *invalid[] = {NULL,
                             "",
                             "00:00:00:00:00:00",
                             "FF:FF:FF:FF:FF:FF",
                             "01:11:22:33:44:55",
                             "AA:BB",
                             "A:BB:CC:00:00:01",
                             "AAA:BB:CC:00:00:01",
                             "AA:BB:CC:00:00:0G",
                             "AA-BB-CC-00-00-01",
                             "AA:BB:CC:00:00:01:02",
                             " AA:BB:CC:00:00:01",
                             "AA:BB:CC:00:00:01 ",
                             "AA:BB:CC:00:00:01\n\n",
                             "AA:BB:CC:00:00:01\r",
                             "AA:BB:CC:00:00:01\nx"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        strcpy(output, "old-value");
        CHECK(!wifi_hotspot_ssid_from_mac(invalid[i], output, sizeof(output)) && !*output);
    }
    char exact[sizeof("mybot-ffff")];
    CHECK(wifi_hotspot_ssid_from_mac(vectors[0].mac, exact, sizeof(exact)));
    CHECK(!strcmp(exact, vectors[0].ssid));
    CHECK(!wifi_hotspot_ssid_from_mac(vectors[0].mac, output, 10) && !*output);
    output[0] = 'x';
    CHECK(!wifi_hotspot_ssid_from_mac(vectors[0].mac, output, 0) && output[0] == 'x');
    CHECK(!wifi_hotspot_ssid_from_mac(vectors[0].mac, NULL, sizeof(output)));
}

static void credentials(void) {
    wifi_credential_t credential;
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_make_credential("IEEE", "password", &credential, error));
    CHECK(!strcmp(credential.psk,
                  "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    CHECK(wifi_make_credential("中文网络", "12345678", &credential, error));
    CHECK(strlen(credential.psk) == 64);
    CHECK(wifi_make_credential("open", "", &credential, error) && !credential.psk[0]);
    char raw[65], lower[65], long_ssid[34], invalid_psk[65], long_interface[17];
    memset(raw, 'A', 64);
    raw[64] = 0;
    memset(lower, 'a', 64);
    lower[64] = 0;
    memset(long_ssid, 'a', 33);
    long_ssid[33] = 0;
    memset(invalid_psk, 'g', 64);
    invalid_psk[64] = 0;
    memset(long_interface, 'a', 16);
    long_interface[16] = 0;
    CHECK(wifi_make_credential("raw", raw, &credential, error) && !strcmp(credential.psk, lower));
    CHECK(!wifi_make_credential("", "password", &credential, error));
    CHECK(!wifi_make_credential(long_ssid, "password", &credential, error));
    CHECK(!wifi_make_credential("ssid", "short", &credential, error));
    CHECK(!wifi_make_credential("ssid", invalid_psk, &credential, error));
    CHECK(!wifi_make_credential("ssid", "pass\0word", &credential, error));
    CHECK(!wifi_make_credential("injected\nnetwork={", "password", &credential, error));
    /* Encoded input must reject NUL before becoming a C-string SSID. */
    char decoded[33];
    CHECK(!wifi_decode_wpa("zero\\x00end", decoded, sizeof(decoded)));
    CHECK(!wifi_valid_ssid("\xc0\x80"));
    CHECK(!wifi_valid_ssid("\xed\xa0\x80"));
    CHECK(!wifi_valid_ssid("\xf4\x90\x80\x80"));
    CHECK(!wifi_valid_ssid("\xc2\x85"));
    CHECK(wifi_valid_interface("wlan0"));
    CHECK(!wifi_valid_interface("wlan0;reboot"));
    CHECK(!wifi_valid_interface("../../wlan0"));
    CHECK(!wifi_valid_interface(long_interface));
}

static void parsing(void) {
    char decoded[128];
    size_t length = 0;
    CHECK(wifi_decode_hex("c3a9", decoded, sizeof(decoded), &length) && !strcmp(decoded, "é"));
    CHECK(length == 2);
    CHECK(!wifi_decode_hex("0", decoded, sizeof(decoded), NULL));
    CHECK(!wifi_decode_hex("x1", decoded, sizeof(decoded), NULL));
    CHECK(wifi_decode_wpa("my\\x20wifi\\\\name", decoded, sizeof(decoded)));
    CHECK(!strcmp(decoded, "my wifi\\name"));
    CHECK(!wifi_decode_wpa("bad\\x0", decoded, sizeof(decoded)));
    CHECK(!wifi_decode_wpa("bad\\q", decoded, sizeof(decoded)));
    const char *scan = "bssid / frequency / signal level / flags / ssid\n"
                       "00:01:02:03:04:05\t2412\t-81\t[WPA2-PSK-CCMP][ESS]\thome\\x20network\n"
                       "00:01:02:03:04:06\t2462\t-40\t[WPA2-PSK-CCMP][ESS]\thome network\n"
                       "00:01:02:03:04:07\t5180\t-20\t[SAE-CCMP][ESS]\tmodern\n"
                       "00:01:02:03:04:08\t2484\t-60\t[ESS]\tpublic\n"
                       "00:01:02:03:04:09\t2412\t-10\t[ESS]\tbad\\x0aSSID\n"
                       "00:01:02:03:04:0a\txyz\t-50\t[ESS]\tbad number\n"
                       "00:01:02:03:04:0b\t2412\t999\t[ESS]\tbad rssi\n"
                       "00:01:02:03:04:0c\t2412\t-20\t[ESS]\t\n";
    wifi_scan_t points;
    wifi_parse_scan_results(scan, &points);
    CHECK(points.count == 3);
    CHECK(!strcmp(points.items[0].ssid, "modern") && points.items[0].channel == 36 &&
          points.items[0].authmode == 6);
    CHECK(!strcmp(points.items[1].ssid, "home network") && points.items[1].rssi == -40 &&
          points.items[1].channel == 11 && points.items[1].authmode == 3);
    CHECK(!strcmp(points.items[2].ssid, "public") && points.items[2].channel == 14 &&
          points.items[2].authmode == 0);
    CHECK(wifi_frequency_channel(5955) == 1);
    CHECK(wifi_frequency_channel(2413) == 0);
    const char *profiles = "network id / ssid / bssid / flags\n"
                           "7\thome\\x20network\tany\t[CURRENT]\n8\tother\tany\t\n";
    char id[9];
    CHECK(wifi_network_id_for_ssid(profiles, "home network", id) && !strcmp(id, "7"));
    CHECK(wifi_network_id_for_ssid(profiles, "other", id) && !strcmp(id, "8"));
    CHECK(!wifi_network_id_for_ssid(profiles, "missing", id));
    CHECK(!wifi_network_id_for_ssid("7;bad\tother\tany\t\n", "other", id));
}

static bool reuse_lease(const char *before, const char *after) {
    return wifi_same_association_lease(before, after, "home network", "42", "routed-ipv4",
                                       "routed-ipv4");
}

static void same_network_lease(void) {
    const char *original = "wpa_state=COMPLETED\nssid=home\\x20network\n"
                           "bssid=02:11:22:33:44:AA\nid=7\n";
    const char *current = "wpa_state=COMPLETED\nssid=home network\n"
                          "bssid=02:11:22:33:44:aa\nid=42\n";
    CHECK(reuse_lease(original, current));
    CHECK(!wifi_same_association_lease(original, current, "home network", "42", "", ""));
    CHECK(!wifi_same_association_lease(original, current, "home network", "42", "old-route",
                                       "new-route"));
    CHECK(!reuse_lease(original, "wpa_state=COMPLETED\nssid=other-network\n"
                                 "bssid=02:11:22:33:44:aa\nid=42\n"));
    CHECK(!reuse_lease("wpa_state=COMPLETED\nssid=other-network\n"
                       "bssid=02:11:22:33:44:aa\nid=7\n",
                       current));
    CHECK(!reuse_lease(original, "wpa_state=COMPLETED\nssid=home network\n"
                                 "bssid=02:11:22:33:44:ab\nid=42\n"));
    CHECK(!reuse_lease(original, "wpa_state=COMPLETED\nssid=home network\nbssid=\nid=42\n"));
    CHECK(!reuse_lease("wpa_state=COMPLETED\nssid=home network\nbssid=\nid=7\n", current));
    CHECK(!reuse_lease("wpa_state=ASSOCIATING\nssid=home network\n"
                       "bssid=02:11:22:33:44:aa\nid=7\n",
                       current));
    CHECK(!reuse_lease(original, "wpa_state=ASSOCIATING\nssid=home network\n"
                                 "bssid=02:11:22:33:44:aa\nid=42\n"));
    CHECK(!reuse_lease(original, "wpa_state=COMPLETED\nssid=home network\n"
                                 "bssid=02:11:22:33:44:aa\nid=7\n"));
}

static bool title_used(const char *title, const char *executable, const char *comm) {
    const char *arguments[] = {title, NULL};
    return wifi_process_identity_uses_interface("dhcpcd", "wlan0", executable, comm, arguments);
}

static void daemon_process_titles(void) {
    CHECK(title_used("dhcpcd: [manager] [ip4] [ip6]", "/usr/sbin/dhcpcd", ""));
    CHECK(title_used("dhcpcd: [manager] [ip4] [ip6]", "/usr/sbin/dhcpcd (deleted)", ""));
    CHECK(title_used("dhcpcd: [privileged proxy]", "/usr/sbin/dhcpcd", ""));
    CHECK(title_used("dhcpcd: [network proxy]", "", "dhcpcd"));
    CHECK(!title_used("dhcpcd: [BPF ARP] wlan0 192.0.2.10", "/usr/sbin/dhcpcd", "dhcpcd"));
    CHECK(!title_used("dhcpcd: [BPF BOOTP] wlan0", "/usr/sbin/dhcpcd", "dhcpcd"));
    CHECK(!title_used("dhcpcd: [manager] [ip4]", "/usr/bin/other", "other"));
    CHECK(!title_used("dhcpcd: [manager] [ip4]", "", ""));
    CHECK(!title_used("dhcpcd: [manager]injected", "/usr/sbin/dhcpcd", ""));
    const char *dhcp[] = {"/usr/sbin/dhcpcd", "-AL", NULL};
    const char *wpa[] = {"/usr/sbin/wpa_supplicant", "-i", "wlan0", NULL};
    const char *other[] = {"wpa_supplicant", "-iwlan1", NULL};
    const char *empty[] = {NULL};
    CHECK(wifi_process_identity_uses_interface("dhcpcd", "wlan0", "", "", dhcp));
    CHECK(wifi_process_identity_uses_interface("wpa_supplicant", "wlan0", "", "", wpa));
    CHECK(!wifi_process_identity_uses_interface("wpa_supplicant", "wlan0", "", "", other));
    CHECK(!wifi_process_identity_uses_interface("dhcpcd", "wlan0", "", "", empty));
}

static void ap_radio_selection(void) {
    const char *capabilities =
        "Wiphy phy0\n Supported interface modes:\n  * managed\n  * AP\n"
        " valid interface combinations:\n  * #{ managed } <= 1, #{ AP } <= 1,\n"
        "    #{ P2P-client, P2P-GO } <= 2, total <= 4, #channels <= 3\n";
    CHECK(wifi_managed_ap_multiple_channels(capabilities));
    CHECK(!wifi_managed_ap_multiple_channels("valid interface combinations:\n * #{ managed } <= "
                                             "1, #{ AP } <= 1, total <= 2, #channels <= 1\n"));
    CHECK(!wifi_managed_ap_multiple_channels(
        "valid interface combinations:\n * #{ managed, AP } <= 1, total <= 4, #channels <= 3\n"));
    CHECK(wifi_managed_ap_multiple_channels(
        "valid interface combinations:\n * #{ managed, AP } <= 2, total <= 2, #channels <= 2\n"));
    CHECK(!wifi_managed_ap_multiple_channels(
        "valid interface combinations:\n * #{ managed } <= 1, total <= 1, #channels <= 3\n"
        " * #{ AP } <= 1, total <= 1, #channels <= 3\n"));
    CHECK(!wifi_managed_ap_multiple_channels("valid interface combinations:\n * #{ managed } <= "
                                             "1, #{ P2P-GO } <= 1, total <= 2, #channels <= 3\n"));
    CHECK(!wifi_managed_ap_multiple_channels("Supported interface modes:\n * managed\n * AP\n"));
    char phy[16], error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_wiphy_from_interface_info("Interface wlan0\n\twiphy 0\n\tchannel 149\n", phy,
                                         sizeof(phy)) &&
          !strcmp(phy, "phy0"));
    CHECK(wifi_wiphy_from_interface_info("wiphy 12\n", phy, sizeof(phy)) && !strcmp(phy, "phy12"));
    CHECK(!wifi_wiphy_from_interface_info("wiphy invalid\n", phy, sizeof(phy)));
    CHECK(!wifi_wiphy_from_interface_info("wiphy 0;injected\n", phy, sizeof(phy)));
    wifi_ap_radio_plan_t plan;
    CHECK(wifi_select_ap_radio(5745, true, &plan, error) && plan.hw_mode == 'g' &&
          plan.channel == 1);
    CHECK(wifi_select_ap_radio(5745, false, &plan, error) && plan.hw_mode == 'a' &&
          plan.channel == 149);
    const int frequencies[] = {5180, 5200, 5220, 5240, 5745, 5765, 5785, 5805, 5825};
    for (size_t i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); ++i) {
        CHECK(wifi_select_ap_radio(frequencies[i], false, &plan, error));
        CHECK(plan.hw_mode == 'a' && plan.channel == wifi_frequency_channel(frequencies[i]));
    }
    CHECK(!wifi_select_ap_radio(5260, false, &plan, error) && error[0]);
    CHECK(wifi_select_ap_radio(5260, true, &plan, error) && plan.hw_mode == 'g' &&
          plan.channel == 1);
    CHECK(wifi_select_ap_radio(2437, false, &plan, error) && plan.hw_mode == 'g' &&
          plan.channel == 6);
    CHECK(wifi_select_ap_radio(2412, true, &plan, error) && plan.hw_mode == 'g' &&
          plan.channel == 1);
    CHECK(wifi_frequency_channel(5955) == 1);
    CHECK(!wifi_select_ap_radio(5955, false, &plan, error) && strstr(error, "6 GHz"));
    CHECK(wifi_select_ap_radio(5955, true, &plan, error) && plan.hw_mode == 'g' &&
          plan.channel == 1);
    CHECK(!wifi_select_ap_radio(7115, false, &plan, error));
    CHECK(wifi_select_ap_radio(0, false, &plan, error) && plan.hw_mode == 'g' && plan.channel == 1);
    CHECK(!wifi_select_ap_radio(2500, false, &plan, error));
}

static void store(void) {
    char temporary[] = "/tmp/mybot-wifi-store-XXXXXX";
    CHECK(mkdtemp(temporary));
    const int directory = open(temporary, O_RDONLY | O_DIRECTORY);
    CHECK(directory >= 0);
    wifi_credentials_t values = {0}, profiles = {0};
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_read_credentials(directory, &values, error) && !values.count);
    CHECK(wifi_make_credential("中文网络", "password", &profiles.items[0], error));
    CHECK(wifi_make_credential("ssid\"; / !", "", &profiles.items[1], error));
    profiles.count = 2;
    CHECK(wifi_write_credentials(directory, &profiles, error));
    struct stat st;
    CHECK(!fstatat(directory, "wifi.credentials", &st, 0) && (st.st_mode & 0777) == 0600);
    CHECK(wifi_read_credentials(directory, &values, error));
    CHECK(values.count == 2 && !strcmp(values.items[0].ssid, profiles.items[0].ssid) &&
          !strcmp(values.items[0].psk, profiles.items[0].psk));
    char path[PATH_MAX];
    CHECK(snprintf(path, sizeof(path), "%s/wifi.credentials", temporary) > 0);
    wifi_buffer_t bytes;
    wifi_buffer_init(&bytes, 16384);
    CHECK(wifi_read_text(path, &bytes));
    CHECK(!strstr(bytes.data, "password") && !strstr(bytes.data, profiles.items[0].ssid));
    wifi_buffer_free(&bytes);
    const wifi_credentials_t duplicates = {2, {profiles.items[0], profiles.items[0]}};
    CHECK(!wifi_write_credentials(directory, &duplicates, error));
    const wifi_credentials_t excessive = {.count = WIFI_MAX_NETWORKS + 1};
    CHECK(!wifi_write_credentials(directory, &excessive, error));
    CHECK(wifi_read_credentials(directory, &values, error) && values.count == 2);
    CHECK(!fchmodat(directory, "wifi.credentials", 0644, 0));
    CHECK(!wifi_read_credentials(directory, &values, error));
    CHECK(!fchmodat(directory, "wifi.credentials", 0600, 0));
    CHECK(!unlinkat(directory, "wifi.credentials", 0));
    CHECK(!symlinkat("/dev/null", directory, "wifi.credentials"));
    CHECK(!wifi_read_credentials(directory, &values, error));
    CHECK(!wifi_write_credentials(directory, &profiles, error));
    CHECK(!unlinkat(directory, "wifi.credentials", 0));
    const wifi_credentials_t empty = {0};
    CHECK(wifi_write_credentials(directory, &empty, error));
    CHECK(wifi_read_credentials(directory, &values, error) && !values.count);
    CHECK(!unlinkat(directory, "wifi.credentials", 0));
    char temporary_name[64];
    CHECK(snprintf(temporary_name, sizeof(temporary_name), "wifi.credentials.new-%ld",
                   (long)getpid()) > 0);
    CHECK(!symlinkat("/dev/null", directory, temporary_name));
    CHECK(!wifi_write_credentials(directory, &profiles, error));
    CHECK(!unlinkat(directory, temporary_name, 0));
    const int corrupt = openat(directory, "wifi.credentials", O_CREAT | O_WRONLY, 0600);
    CHECK(corrupt >= 0);
    const char malformed[] = "MYBOT_WIFI_V1\nzz\t\n";
    CHECK(write(corrupt, malformed, sizeof(malformed) - 1) == (ssize_t)(sizeof(malformed) - 1));
    CHECK(!close(corrupt));
    CHECK(!wifi_read_credentials(directory, &values, error));
    CHECK(!unlinkat(directory, "wifi.credentials", 0));
    CHECK(!close(directory) && !rmdir(temporary));
}
typedef struct {
    pthread_mutex_t mutex;
    bool requested;
} cancellation_t;
static bool is_cancelled(void *context) {
    cancellation_t *cancel = context;
    CHECK(!pthread_mutex_lock(&cancel->mutex));
    const bool result = cancel->requested;
    CHECK(!pthread_mutex_unlock(&cancel->mutex));
    return result;
}

static void *request_cancel(void *context) {
    cancellation_t *cancel = context;
    wifi_sleep_ms(50);
    CHECK(!pthread_mutex_lock(&cancel->mutex));
    cancel->requested = true;
    CHECK(!pthread_mutex_unlock(&cancel->mutex));
    return NULL;
}

static void processes(const char *self) {
    wifi_buffer_t output;
    wifi_buffer_init(&output, WIFI_COMMAND_OUTPUT_LIMIT);
    char error[WIFI_ERROR_CAPACITY] = "";
    const char *echo[] = {"/bin/echo", "$(touch /tmp/unsafe); secret ' \"", NULL};
    CHECK(wifi_command(echo, 1000, NULL, &output, error));
    CHECK(!strcmp(output.data, "$(touch /tmp/unsafe); secret ' \"\n"));
    const char *missing[] = {"/nonexistent/mybot-helper", NULL};
    CHECK(!wifi_command(missing, 1000, NULL, &output, error));
    const char *ignore[] = {self, "--ignore-term", NULL};
    uint64_t start = wifi_now_ms();
    CHECK(!wifi_command(ignore, 80, NULL, &output, error));
    CHECK(wifi_now_ms() - start < 2000 && strstr(error, "timed out"));
    cancellation_t cancel = {PTHREAD_MUTEX_INITIALIZER, false};
    const wifi_cancel_t callback = {is_cancelled, &cancel};
    pthread_t canceller;
    CHECK(!pthread_create(&canceller, NULL, request_cancel, &cancel));
    start = wifi_now_ms();
    const bool completed = wifi_command(ignore, 10000, &callback, &output, error);
    CHECK(!pthread_join(canceller, NULL));
    CHECK(!completed && wifi_now_ms() - start < 2000 && strstr(error, "cancelled"));
    CHECK(!pthread_mutex_destroy(&cancel.mutex));
    const char *flood[] = {self, "--flood", NULL};
    CHECK(!wifi_command(flood, 3000, NULL, &output, error));
    CHECK(output.length <= WIFI_COMMAND_OUTPUT_LIMIT && strstr(error, "exceeded"));
    wifi_buffer_free(&output);
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
}
typedef struct {
    pthread_mutex_t mutex;
    bool stop, replace_endpoint, reject_reload;
    unsigned reloads;
    int descriptor;
    char remote[108];
} supplicant_t;
static int bind_control(const char *remote) {
    const int descriptor = socket(AF_UNIX, SOCK_DGRAM, 0);
    CHECK(descriptor >= 0);
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    CHECK(wifi_copy(address.sun_path, sizeof(address.sun_path), remote));
    CHECK(!bind(descriptor, (struct sockaddr *)&address, sizeof(address)));
    return descriptor;
}

static bool supplicant_stopped(supplicant_t *s) {
    CHECK(!pthread_mutex_lock(&s->mutex));
    const bool result = s->stop;
    CHECK(!pthread_mutex_unlock(&s->mutex));
    return result;
}

static void *supplicant_worker(void *context) {
    supplicant_t *s = context;
    while (!supplicant_stopped(s)) {
        struct pollfd ready = {s->descriptor, POLLIN, 0};
        if (poll(&ready, 1, 50) <= 0)
            continue;
        struct sockaddr_un peer = {0};
        socklen_t length = sizeof(peer);
        char bytes[65];
        const ssize_t used =
            recvfrom(s->descriptor, bytes, sizeof(bytes) - 1, 0, (struct sockaddr *)&peer, &length);
        CHECK(used > 0);
        bytes[used] = 0;
        const char *response;
        if (!strcmp(bytes, "PING"))
            response = "PONG\n";
        else {
            CHECK(!strcmp(bytes, "RECONFIGURE"));
            ++s->reloads;
            if (s->replace_endpoint && s->reloads == 1) {
                CHECK(!close(s->descriptor) && !unlink(s->remote));
                s->descriptor = bind_control(s->remote);
                continue;
            }
            response = s->reject_reload ? "FAIL\n" : "OK\n";
        }
        CHECK(sendto(s->descriptor, response, strlen(response), 0, (struct sockaddr *)&peer,
                     length) == (ssize_t)strlen(response));
    }
    return NULL;
}

static void control_reload(bool replace_endpoint, bool reject_reload) {
    char temporary[] = "/tmp/mybot-wpa-control-XXXXXX";
    CHECK(mkdtemp(temporary));
    supplicant_t s = {.mutex = PTHREAD_MUTEX_INITIALIZER,
                      .replace_endpoint = replace_endpoint,
                      .reject_reload = reject_reload};
    CHECK(snprintf(s.remote, sizeof(s.remote), "%s/server", temporary) > 0);
    char local[108];
    CHECK(snprintf(local, sizeof(local), "%s/client", temporary) > 0);
    s.descriptor = bind_control(s.remote);
    pthread_t worker;
    CHECK(!pthread_create(&worker, NULL, supplicant_worker, &s));
    const char *remotes[] = {s.remote, NULL};
    const bool accepted = wifi_reconfigure_control(remotes, local, 2500);
    CHECK(!pthread_mutex_lock(&s.mutex));
    s.stop = true;
    CHECK(!pthread_mutex_unlock(&s.mutex));
    CHECK(!pthread_join(worker, NULL));
    CHECK(accepted == !reject_reload && s.reloads == (replace_endpoint ? 2U : 1U));
    CHECK(!close(s.descriptor) && !unlink(s.remote));
    CHECK(access(local, F_OK) != 0);
    CHECK(!rmdir(temporary) && !pthread_mutex_destroy(&s.mutex));
}
typedef struct {
    char commands[4][40];
    unsigned count;
    bool accepted;
} command_capture_t;
static bool capture_command(void *context, const char *request) {
    command_capture_t *capture = context;
    CHECK(capture->count < 4);
    CHECK(wifi_copy(capture->commands[capture->count++], sizeof(capture->commands[0]), request));
    return capture->accepted;
}

static void committed_selection(void) {
    const char *networks = "network id / ssid / bssid / flags\n"
                           "42\trequested\tany\t\n9\tother\tany\t[CURRENT]\n";
    command_capture_t capture = {.accepted = true};
    bool reassociated = false;
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_ensure_committed_selection("wpa_state=COMPLETED\nssid=other\nid=9\n", networks,
                                          "requested", capture_command, &capture, &reassociated,
                                          error));
    CHECK(reassociated && capture.count == 2 && !strcmp(capture.commands[0], "ENABLE_NETWORK 42") &&
          !strcmp(capture.commands[1], "REASSOCIATE"));
    capture.count = 0;
    const char *states[] = {"COMPLETED", "ASSOCIATING", "4WAY_HANDSHAKE"};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        char current[100];
        CHECK(snprintf(current, sizeof(current), "wpa_state=%s\nssid=requested\nid=42\n",
                       states[i]) > 0);
        CHECK(wifi_ensure_committed_selection(current, networks, "requested", capture_command,
                                              &capture, &reassociated, error));
        CHECK(!reassociated && !capture.count);
    }
    CHECK(!wifi_ensure_committed_selection("wpa_state=SCANNING\n", networks, "missing",
                                           capture_command, &capture, &reassociated, error));
    CHECK(!reassociated && !capture.count && error[0]);
    capture.accepted = false;
    CHECK(!wifi_ensure_committed_selection("wpa_state=COMPLETED\nssid=other\nid=9\n", networks,
                                           "requested", capture_command, &capture, &reassociated,
                                           error));
    CHECK(!reassociated && capture.count == 1 && !strcmp(capture.commands[0], "ENABLE_NETWORK 42"));
    CHECK(!strstr(error, "requested") && !strstr(error, "other"));
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--ignore-term")) {
        signal(SIGTERM, SIG_IGN);
        for (;;)
            pause();
    }
    if (argc == 2 && !strcmp(argv[1], "--flood")) {
        char bytes[4096];
        memset(bytes, 'x', sizeof(bytes));
        for (unsigned index = 0; index < 512; ++index)
            (void)write(STDOUT_FILENO, bytes, sizeof(bytes));
        return 0;
    }
    char self[PATH_MAX];
    CHECK(realpath(argv[0], self));
    hotspot_names();
    credentials();
    parsing();
    same_network_lease();
    daemon_process_titles();
    ap_radio_selection();
    store();
    control_reload(false, false);
    control_reload(true, false);
    control_reload(false, true);
    committed_selection();
    processes(self);
    const wifi_backend_config_t config = {.interface_name = "wlan0;reboot"};
    wifi_backend_t backend = {0};
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_linux_backend_create(&config, &backend, error));
    CHECK(backend.ops->initialize(backend.context, error) == WIFI_BACKEND_FAILED && error[0]);
    CHECK(!backend.ops->online(backend.context));
    wifi_backend_destroy(&backend);
#ifdef __linux__
    /* A valid interface name can precede registration of the kernel's Wi-Fi device. */
    char missing[16];
    snprintf(missing, sizeof(missing), "testw%08x", (unsigned)getpid());
    const wifi_backend_config_t delayed = {.interface_name = missing};
    CHECK(wifi_linux_backend_create(&delayed, &backend, error));
    CHECK(backend.ops->initialize(backend.context, error) == WIFI_BACKEND_RETRY && error[0]);
    wifi_backend_destroy(&backend);
#endif
    puts("Wi-Fi credentials, AP radio, control reload and owned cleanup: PASS");
    return 0;
}
