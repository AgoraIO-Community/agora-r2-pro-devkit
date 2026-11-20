/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "check.h"
#include "system_wifi_profile.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char original[] = "# Imported locally; never print credentials\ncountry=CN\n"
                               "ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\n"
                               "network={\n ssid=\"IEEE\" # trailing comment\n psk=\"password\"\n"
                               " key_mgmt=WPA-PSK\n priority=2\n proto=RSN\n}\n"
                               "network = {\n ssid=6f70656e\n key_mgmt=NONE\n priority=9\n}\n";
static char *file_text(const char *path) {
    FILE *file = fopen(path, "rb");
    CHECK(file);
    CHECK(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    CHECK(length >= 0 && length < 65537);
    CHECK(!fseek(file, 0, SEEK_SET));
    char *text = malloc((size_t)length + 1);
    CHECK(text);
    CHECK(fread(text, 1, (size_t)length, file) == (size_t)length);
    text[length] = '\0';
    CHECK(!fclose(file));
    return text;
}

static void write_text(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(fd >= 0 && !fchmod(fd, 0600));
    CHECK(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    CHECK(!close(fd));
}
typedef struct {
    const char *path;
    int reloads;
    bool reject_first;
} reload_t;
static bool reload(void *context) {
    reload_t *state = context;
    ++state->reloads;
    if (state->reject_first)
        return state->reloads != 1;
    char *text = file_text(state->path);
    CHECK(strstr(text, "ctrl_interface=/var/run/wpa_supplicant\n"));
    CHECK(strstr(text, "update_config=0\n") && strstr(text, "country=CN\n") &&
          strstr(text, "proto=RSN\n"));
    CHECK(!strstr(text, "password"));
    free(text);
    return true;
}

static void system_profiles(void) {
    wifi_credentials_t profiles = {0};
    char error[WIFI_ERROR_CAPACITY];
    CHECK(wifi_parse_system_profiles(original, &profiles, error));
    CHECK(profiles.count == 2 && !strcmp(profiles.items[0].ssid, "open") &&
          !*profiles.items[0].psk);
    CHECK(!strcmp(profiles.items[1].ssid, "IEEE") &&
          !strcmp(profiles.items[1].psk,
                  "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    const char *quoted =
        "network={\nssid=\"quote\\\"# space\\x20name\"\npsk=\"brace{}#password\"\n}\n";
    wifi_credentials_t escaped = {0};
    CHECK(wifi_parse_system_profiles(quoted, &escaped, error));
    CHECK(escaped.count == 1 && !strcmp(escaped.items[0].ssid, "quote\"# space name"));
    const char *bad[] = {"network={\nssid=\"safe\"\nkey_mgmt=WPA-EAP\n}\n",
                         "network={\nssid=\"safe\"\nkey_mgmt=SAE\npsk=\"password\"\n}\n",
                         "network={\nssid=\"safe\"\nkey_mgmt=NONE\ndisabled=1\n}\n",
                         "network={\nssid=\"safe\"\nkey_mgmt=NONE\nssid=\"duplicate\"\n}\n",
                         "network={\nssid=\"safe\"\npsk=plain-text-without-quotes\n}\n",
                         "network={\nssid=\"safe\"\npsk=\"unterminated\n}\n",
                         "include=/tmp/other.conf\n",
                         "network={\nssid=\"safe\"\nkey_mgmt=NONE\n",
                         "network={\nssid=\"safe\\x00hidden\"\nkey_mgmt=NONE\n}\n"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        CHECK(!wifi_parse_system_profiles(bad[i], &profiles, error));
        CHECK(*error && profiles.count == 2 && !strcmp(profiles.items[0].ssid, "open"));
        CHECK(!strstr(error, "password"));
    }
    char duplicate[sizeof original * 2];
    snprintf(duplicate, sizeof duplicate, "%s%s", original, original);
    CHECK(!wifi_parse_system_profiles(duplicate, &profiles, error));
    CHECK(profiles.count == 2 && !strcmp(profiles.items[0].ssid, "open"));
    char temporary[] = "/tmp/mybot-system-profile-XXXXXX";
    CHECK(mkdtemp(temporary));
    char directory[PATH_MAX], path[PATH_MAX];
    CHECK(realpath(temporary, directory));
    CHECK(snprintf(path, sizeof path, "%s/wpa.conf", directory) > 0);
    write_text(path, original);
    wifi_credentials_t loaded = {0};
    CHECK(wifi_read_system_profiles(path, &loaded, error) && loaded.count == 2);
    wifi_credential_t swap = profiles.items[0];
    profiles.items[0] = profiles.items[1];
    profiles.items[1] = swap;
    reload_t state = {.path = path};
    CHECK(wifi_commit_system_profiles(path, &profiles, reload, &state, error) &&
          state.reloads == 1);
    CHECK(wifi_read_system_profiles(path, &loaded, error));
    CHECK(loaded.count == 2 && !strcmp(loaded.items[0].ssid, "IEEE"));
    struct stat mode;
    CHECK(!stat(path, &mode) && (mode.st_mode & 0777) == 0600);
    char *committed = file_text(path);
    state.reloads = 0;
    state.reject_first = true;
    wifi_credentials_t one = {.count = 1};
    one.items[0] = profiles.items[1];
    CHECK(!wifi_commit_system_profiles(path, &one, reload, &state, error));
    char *restored = file_text(path);
    CHECK(state.reloads == 2 && !strcmp(restored, committed));
    free(restored);
    CHECK(strstr(error, "restored"));
    state.reloads = 0;
    state.reject_first = false;
    wifi_credentials_t repeated = {.count = 2};
    repeated.items[0] = repeated.items[1] = profiles.items[0];
    CHECK(!wifi_commit_system_profiles(path, &repeated, reload, &state, error) && !state.reloads);
    restored = file_text(path);
    CHECK(!strcmp(restored, committed));
    free(restored);
    CHECK(!chmod(directory, 0500));
    wifi_credentials_t empty = {0};
    CHECK(!wifi_commit_system_profiles(path, &empty, reload, &state, error));
    CHECK(!chmod(directory, 0700));
    restored = file_text(path);
    CHECK(!strcmp(restored, committed));
    free(restored);
    CHECK(!unlink(path));
    CHECK(!symlink("/dev/null", path));
    CHECK(!wifi_read_system_profiles(path, &loaded, error));
    CHECK(!wifi_commit_system_profiles(path, &empty, reload, &state, error));
    CHECK(!unlink(path) && !mkfifo(path, 0600));
    CHECK(!wifi_read_system_profiles(path, &loaded, error));
    CHECK(!unlink(path));
    int malformed = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(malformed >= 0);
    const char embedded_nul[] = "country=CN\n\0network={\nssid=bad\n}\n";
    CHECK(write(malformed, embedded_nul, sizeof(embedded_nul) - 1) ==
          (ssize_t)sizeof(embedded_nul) - 1);
    CHECK(!close(malformed));
    CHECK(!wifi_read_system_profiles(path, &loaded, error));
    CHECK(!unlink(path) && !rmdir(directory));
    free(committed);
}

static void dhcpcd_events(void) {
    const char *event = "version=1\ngeneration=event-2\nbssid=02:11:22:33:44:AA\n"
                        "ipv4=192.0.2.10\nssid_hex=6e65772d6e6574776f726b\n";
    const char *status = "wpa_state=COMPLETED\nbssid=02:11:22:33:44:aa\nssid=new-network\nid=3\n";
    wifi_dhcpcd_lease_t lease;
    CHECK(wifi_parse_dhcpcd_lease(event, &lease));
    CHECK(!strcmp(lease.bssid, "02:11:22:33:44:aa") && !strcmp(lease.ssid, "new-network"));
    const char *addresses[] = {"192.0.2.10"}, *other[] = {"192.0.2.11"};
    CHECK(wifi_dhcpcd_lease_matches(&lease, "event-1", status, addresses, 1));
    CHECK(!wifi_dhcpcd_lease_matches(&lease, "event-2", status, addresses, 1));
    CHECK(!wifi_dhcpcd_lease_matches(&lease, "event-1", status, other, 1));
    CHECK(!wifi_dhcpcd_lease_matches(&lease, "event-1", "wpa_state=ASSOCIATING\n", addresses, 1));
    CHECK(!wifi_dhcpcd_lease_matches(
        &lease, "event-1", "wpa_state=COMPLETED\nbssid=02:11:22:33:44:bb\nssid=new-network\n",
        addresses, 1));
    CHECK(!wifi_dhcpcd_lease_matches(
        &lease, "event-1", "wpa_state=COMPLETED\nbssid=02:11:22:33:44:aa\nssid=old-network\n",
        addresses, 1));
    const char *minimal =
        "version=1\ngeneration=event-3\nbssid=02:11:22:33:44:aa\nipv4=192.0.2.10\n";
    CHECK(wifi_parse_dhcpcd_lease(minimal, &lease) && !*lease.ssid);
    CHECK(wifi_dhcpcd_lease_matches(&lease, "event-2", status, addresses, 1));
    char bad[3001];
    snprintf(bad, sizeof bad, "%sversion=1\n", event);
    CHECK(!wifi_parse_dhcpcd_lease(bad, &lease));
    snprintf(bad, sizeof bad, "%sextra=x\n", event);
    CHECK(!wifi_parse_dhcpcd_lease(bad, &lease));
    CHECK(!wifi_parse_dhcpcd_lease("version=1\ngeneration=x\n", &lease));
    memset(bad, 'x', 3000);
    bad[3000] = '\0';
    CHECK(!wifi_parse_dhcpcd_lease(bad, &lease));
    char temporary[] = "/tmp/mybot-dhcp-event-XXXXXX";
    CHECK(mkdtemp(temporary));
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/wlan0.lease", temporary);
    write_text(path, event);
    CHECK(wifi_read_dhcpcd_lease(temporary, "wlan0", &lease));
    CHECK(!wifi_read_dhcpcd_lease(temporary, "../wlan0", &lease));
    CHECK(!chmod(path, 0644));
    CHECK(!wifi_read_dhcpcd_lease(temporary, "wlan0", &lease));
    CHECK(!unlink(path));
    CHECK(!symlink("/dev/null", path));
    CHECK(!wifi_read_dhcpcd_lease(temporary, "wlan0", &lease));
    CHECK(!unlink(path) && !rmdir(temporary));
}

static void bounded_text(void) {
    wifi_buffer_t buffer;
    wifi_buffer_init(&buffer, 4);
    CHECK(wifi_buffer_text(&buffer, "test"));
    CHECK(!wifi_buffer_text(&buffer, "x"));
    CHECK(!strcmp(buffer.data, "test"));
    char *text = wifi_buffer_take(&buffer);
    CHECK(text && !strcmp(text, "test") && !buffer.data);
    free(text);
    wifi_buffer_free(&buffer);
    char value[4];
    CHECK(!wifi_field("a=1\na=2\n", "a", value, sizeof value));
    CHECK(!*value);
    CHECK(!wifi_field("a=1234\n", "a", value, sizeof value));
    CHECK(!*value);
    CHECK(wifi_field("a=123\n", "a", value, sizeof value) && !strcmp(value, "123"));
    CHECK(wifi_utf8_valid("测试", strlen("测试")));
    CHECK(!wifi_utf8_valid("\xf0\x80\x80\xaf", 4));
    CHECK(!wifi_utf8_valid("\xed\xa0\x80", 3) && !wifi_utf8_valid("\xf4\x90\x80\x80", 4));
}

int main(void) {
    system_profiles();
    dhcpcd_events();
    bounded_text();
    puts("C system Wi-Fi profile transactions, lease proof and bounded text: PASS");
    return 0;
}
