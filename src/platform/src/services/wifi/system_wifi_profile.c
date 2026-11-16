/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "system_wifi_profile.h"
#include "wifi_backend_internal.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_LIMIT 65536
static const char *const setting_names[] = {"bssid",      "disabled", "group",     "id_str",
                                            "ieee80211w", "key_mgmt", "pairwise",  "priority",
                                            "proto",      "psk",      "scan_ssid", "ssid"};
enum { SETTING_COUNT = sizeof(setting_names) / sizeof(setting_names[0]) };
typedef struct {
    wifi_credential_t credential;
    const char *values[SETTING_COUNT];
    long priority;
} network_t;
typedef struct {
    char *source;
    const char *control;
    wifi_buffer_t globals;
    network_t networks[WIFI_MAX_NETWORKS];
    size_t count;
} document_t;

static void document_free(document_t *document) {
    free(document->source);
    wifi_buffer_free(&document->globals);
    *document = (document_t){0};
}

static char *trim(char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r')
        ++text;
    size_t length = strlen(text);
    while (length &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r'))
        text[--length] = '\0';
    return text;
}

static bool strip_comment(char *line) {
    bool quoted = false, escaped = false;
    for (unsigned char *p = (unsigned char *)line; *p; ++p) {
        if (*p < 0x20 && *p != '\t' && *p != '\r')
            return false;
        if (!quoted && *p == '#') {
            *p = '\0';
            break;
        }
        if (escaped)
            escaped = false;
        else if (quoted && *p == '\\')
            escaped = true;
        else if (*p == '"')
            quoted = !quoted;
    }
    return !quoted && !escaped;
}

static const char *setting(const network_t *network, const char *key) {
    for (size_t i = 0; i < SETTING_COUNT; ++i)
        if (!strcmp(setting_names[i], key))
            return network->values[i];
    return NULL;
}

static bool quoted_value(const char *value, char *output, size_t capacity) {
    size_t length = strlen(value);
    if (length < 2 || value[0] != '"' || value[length - 1] != '"')
        return false;
    for (size_t i = 1; i < length - 1; ++i) {
        if (value[i] == '\\') {
            if (++i == length - 1)
                return false;
        } else if (value[i] == '"')
            return false;
    }
    char *inner = strndup(value + 1, length - 2);
    bool ok = inner && wifi_decode_wpa(inner, output, capacity);
    free(inner);
    return ok;
}

static bool finish_network(document_t *document, network_t *network, char *error) {
    char ssid[WIFI_SSID_CAPACITY], password[WIFI_PSK_CAPACITY] = "";
    const char *encoded = setting(network, "ssid"), *key = setting(network, "psk"),
               *security = setting(network, "key_mgmt");
    size_t decoded = 0;
    bool valid =
        encoded && (encoded[0] == '"' ? quoted_value(encoded, ssid, sizeof ssid)
                                      : wifi_decode_hex(encoded, ssid, sizeof ssid, &decoded) &&
                                            decoded == strlen(ssid));
    if (!valid || !wifi_valid_ssid(ssid)) {
        wifi_error(error, "Invalid or duplicate network identity in system Wi-Fi configuration");
        return false;
    }
    for (size_t i = 0; i < document->count; ++i)
        if (!strcmp(document->networks[i].credential.ssid, ssid)) {
            wifi_error(error,
                       "Invalid or duplicate network identity in system Wi-Fi configuration");
            return false;
        }
    if (!security)
        security = "WPA-PSK";
    if ((strcmp(security, "NONE") && strcmp(security, "WPA-PSK")) ||
        (!strcmp(security, "NONE") && key) || (!strcmp(security, "WPA-PSK") && !key)) {
        wifi_error(error, "System Wi-Fi import supports only open and WPA-PSK networks");
        return false;
    }
    if (key) {
        if (key[0] == '"') {
            if (!quoted_value(key, password, sizeof password)) {
                wifi_error(error, "Invalid credential encoding in system Wi-Fi configuration");
                return false;
            }
        } else {
            if (strlen(key) != 64) {
                wifi_error(error, "Invalid derived credential in system Wi-Fi configuration");
                return false;
            }
            for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
                if (!isxdigit(*p)) {
                    wifi_error(error, "Invalid derived credential in system Wi-Fi configuration");
                    return false;
                }
            wifi_copy(password, sizeof password, key);
        }
    }
    if (!wifi_make_credential(ssid, password, &network->credential, error))
        return false;
    const char *priority = setting(network, "priority");
    if (priority) {
        char *end = NULL;
        errno = 0;
        network->priority = strtol(priority, &end, 10);
        if (errno || !end || *end || network->priority < INT_MIN || network->priority > INT_MAX) {
            wifi_error(error, "Invalid network priority in system Wi-Fi configuration");
            return false;
        }
    }
    if (document->count == WIFI_MAX_NETWORKS) {
        wifi_error(error,
                   "System Wi-Fi import supports at most 10 networks; no records were changed");
        return false;
    }
    document->networks[document->count++] = *network;
    *network = (network_t){0};
    return true;
}

static bool parse_document(const char *text, document_t *document, char *error) {
    wifi_buffer_init(&document->globals, CONFIG_LIMIT);
    if (!text || strlen(text) > CONFIG_LIMIT || !(document->source = strdup(text))) {
        wifi_error(error, "Cannot read bounded system Wi-Fi configuration");
        return false;
    }
    network_t current = {0};
    bool inside = false;
    char *save = NULL;
    for (char *raw = strtok_r(document->source, "\n", &save); raw;
         raw = strtok_r(NULL, "\n", &save)) {
        if (!strip_comment(raw)) {
            wifi_error(error, "Invalid quoting or control character in system Wi-Fi configuration");
            return false;
        }
        char *line = trim(raw);
        if (!*line)
            continue;
        if (!strcmp(line, "}")) {
            if (!inside) {
                wifi_error(error, "Unexpected network block end in system Wi-Fi configuration");
                return false;
            }
            if (!finish_network(document, &current, error))
                return false;
            inside = false;
            continue;
        }
        char *equal = strchr(line, '=');
        if (!equal) {
            wifi_error(error, "Invalid assignment in system Wi-Fi configuration");
            return false;
        }
        *equal = '\0';
        char *key = trim(line), *value = trim(equal + 1);
        if (!*key || !*value) {
            wifi_error(error, "Invalid assignment in system Wi-Fi configuration");
            return false;
        }
        for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
            if (!isalnum(*p) && *p != '_') {
                wifi_error(error, "Invalid assignment in system Wi-Fi configuration");
                return false;
            }
        if (!inside && !strcmp(key, "network") && !strcmp(value, "{")) {
            inside = true;
            continue;
        }
        if (!strcmp(key, "network") || !strcmp(key, "include") ||
            (value[0] != '"' && (strchr(value, '{') || strchr(value, '}')))) {
            wifi_error(error, "Unsupported nested/imported system Wi-Fi configuration");
            return false;
        }
        if (!inside) {
            if (!strcmp(key, "ctrl_interface")) {
                if (document->control) {
                    wifi_error(error, "Duplicate system Wi-Fi control interface setting");
                    return false;
                }
                document->control = value;
            } else if (strcmp(key, "update_config") &&
                       !wifi_buffer_printf(&document->globals, "%s=%s\n", key, value)) {
                wifi_error(error, "Cannot allocate system Wi-Fi configuration");
                return false;
            }
            continue;
        }
        size_t index = 0;
        while (index < SETTING_COUNT && strcmp(key, setting_names[index]))
            ++index;
        if (index == SETTING_COUNT || current.values[index] ||
            (!strcmp(key, "disabled") && strcmp(value, "0"))) {
            wifi_error(
                error,
                "Unsupported or duplicate network setting; system Wi-Fi configuration retained");
            return false;
        }
        current.values[index] = value;
    }
    if (inside) {
        wifi_error(error, "Unclosed network block in system Wi-Fi configuration");
        return false;
    }
    return true;
}

static bool open_parent(const char *path, int *parent, char *name, char *error) {
    *parent = -1;
    if (!path || path[0] != '/' || strlen(path) >= PATH_MAX || path[strlen(path) - 1] == '/') {
        wifi_error(error, "System Wi-Fi profile path must be an absolute regular file");
        return false;
    }
    char copy[PATH_MAX];
    wifi_copy(copy, sizeof copy, path);
    char *slash = strrchr(copy, '/');
    if (!slash || !slash[1] || !strcmp(slash + 1, ".") || !strcmp(slash + 1, "..")) {
        wifi_error(error, "Unsafe system Wi-Fi profile filename");
        return false;
    }
    wifi_copy(name, NAME_MAX + 1, slash + 1);
    if (strlen(slash + 1) > NAME_MAX) {
        wifi_error(error, "Unsafe system Wi-Fi profile filename");
        return false;
    }
    *slash = '\0';
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    char *component = slash == copy ? slash : copy + 1;
    while (*component && fd >= 0) {
        char *next = strchr(component, '/');
        if (next)
            *next = '\0';
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) {
            close(fd);
            fd = -1;
            break;
        }
        int child = openat(fd, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(fd);
        fd = child;
        if (!next)
            break;
        component = next + 1;
        if (!*component) {
            if (fd >= 0)
                close(fd);
            fd = -1;
            break;
        }
    }
    struct stat status;
    if (fd < 0 || fstat(fd, &status) || !S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
        (status.st_mode & 0022) || !(status.st_mode & 0200)) {
        if (fd >= 0)
            close(fd);
        wifi_error(error,
                   "System Wi-Fi parent directory must be owned and not writable by other users");
        return false;
    }
    *parent = fd;
    return true;
}

static bool read_config(int parent, const char *name, char **text, char *error) {
    int fd = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat status;
    if (fd < 0 || fstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
        (status.st_mode & 0022) || status.st_nlink != 1 || status.st_size < 0 ||
        status.st_size > CONFIG_LIMIT) {
        if (fd >= 0)
            close(fd);
        wifi_error(error, "Cannot read a safe owned system Wi-Fi profile file");
        return false;
    }
    wifi_buffer_t buffer;
    wifi_buffer_init(&buffer, CONFIG_LIMIT);
    char bytes[4096];
    bool ok = true;
    for (;;) {
        ssize_t count = read(fd, bytes, sizeof bytes);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            ok = false;
            break;
        }
        if (!count)
            break;
        if (memchr(bytes, 0, (size_t)count) || !wifi_buffer_append(&buffer, bytes, (size_t)count)) {
            ok = false;
            break;
        }
    }
    close(fd);
    if (ok) {
        *text = wifi_buffer_take(&buffer);
        ok = *text != NULL;
    }
    wifi_buffer_free(&buffer);
    if (!ok)
        wifi_error(error, "Cannot read bounded system Wi-Fi configuration");
    return ok;
}

static bool replace_config(int parent, const char *name, const char *text) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static unsigned long sequence;
    pthread_mutex_lock(&lock);
    unsigned long current = ++sequence;
    pthread_mutex_unlock(&lock);
    char temporary[96];
    snprintf(temporary, sizeof temporary, ".mybot-wpa-%ld-%lu", (long)getpid(), current);
    int fd = openat(parent, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    bool ok = fchmod(fd, 0600) == 0;
    size_t used = 0, length = strlen(text);
    while (ok && used < length) {
        ssize_t count = write(fd, text + used, length - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            ok = false;
        else
            used += (size_t)count;
    }
    if (ok)
        ok = fsync(fd) == 0;
    if (close(fd))
        ok = false;
    if (ok)
        ok = renameat(parent, temporary, parent, name) == 0;
    if (ok)
        ok = fsync(parent) == 0;
    if (!ok)
        unlinkat(parent, temporary, 0);
    return ok;
}

static char *render(const document_t *document, const wifi_credentials_t *profiles) {
    wifi_buffer_t output;
    wifi_buffer_init(&output, CONFIG_LIMIT);
    bool ok = wifi_buffer_printf(
                  &output, "# Managed system Wi-Fi profiles\nctrl_interface=%s\nupdate_config=0\n",
                  document->control ? document->control : "/run/wpa_supplicant") &&
              wifi_buffer_text(&output, document->globals.data ? document->globals.data : "");
    for (size_t i = 0; ok && i < profiles->count; ++i) {
        const wifi_credential_t *profile = &profiles->items[i];
        char encoded[65];
        ok = wifi_encode_hex(profile->ssid, strlen(profile->ssid), encoded, sizeof encoded) &&
             wifi_buffer_printf(&output, "\nnetwork={\n    ssid=%s\n    key_mgmt=%s\n", encoded,
                                *profile->psk ? "WPA-PSK" : "NONE");
        if (ok && *profile->psk)
            ok = wifi_buffer_printf(&output, "    psk=%s\n", profile->psk);
        if (ok)
            ok = wifi_buffer_printf(&output, "    scan_ssid=1\n    priority=%d\n", 100 - (int)i);
        const network_t *previous = NULL;
        for (size_t j = 0; j < document->count; ++j)
            if (!strcmp(document->networks[j].credential.ssid, profile->ssid)) {
                previous = &document->networks[j];
                break;
            }
        for (size_t j = 0; ok && previous && j < SETTING_COUNT; ++j) {
            const char *key = setting_names[j];
            if (previous->values[j] && strcmp(key, "ssid") && strcmp(key, "psk") &&
                strcmp(key, "key_mgmt") && strcmp(key, "scan_ssid") && strcmp(key, "priority") &&
                strcmp(key, "disabled"))
                ok = wifi_buffer_printf(&output, "    %s=%s\n", key, previous->values[j]);
        }
        if (ok)
            ok = wifi_buffer_text(&output, "}\n");
    }
    char *result = ok ? wifi_buffer_take(&output) : NULL;
    wifi_buffer_free(&output);
    return result;
}

bool wifi_parse_system_profiles(const char *text, wifi_credentials_t *profiles, char *error) {
    if (!profiles) {
        wifi_error(error, "Invalid system Wi-Fi profile output");
        return false;
    }
    document_t document = {0};
    bool ok = parse_document(text, &document, error);
    if (ok) {
        for (size_t i = 1; i < document.count; ++i) {
            network_t item = document.networks[i];
            size_t j = i;
            while (j && document.networks[j - 1].priority < item.priority) {
                document.networks[j] = document.networks[j - 1];
                --j;
            }
            document.networks[j] = item;
        }
        wifi_credentials_t parsed = {0};
        parsed.count = document.count;
        for (size_t i = 0; i < document.count; ++i)
            parsed.items[i] = document.networks[i].credential;
        *profiles = parsed;
        wifi_error(error, "");
    }
    document_free(&document);
    return ok;
}

bool wifi_read_system_profiles(const char *path, wifi_credentials_t *profiles, char *error) {
    int parent = -1;
    char name[NAME_MAX + 1], *text = NULL;
    if (!open_parent(path, &parent, name, error))
        return false;
    bool ok = read_config(parent, name, &text, error);
    close(parent);
    if (ok)
        ok = wifi_parse_system_profiles(text, profiles, error);
    free(text);
    return ok;
}

bool wifi_commit_system_profiles(const char *path, const wifi_credentials_t *profiles,
                                 bool (*reconfigure)(void *), void *context, char *error) {
    if (!profiles || profiles->count > WIFI_MAX_NETWORKS || !reconfigure) {
        wifi_error(error, "Invalid system Wi-Fi profile update");
        return false;
    }
    for (size_t i = 0; i < profiles->count; ++i) {
        const wifi_credential_t *profile = &profiles->items[i];
        wifi_credential_t checked;
        if (!memchr(profile->ssid, 0, sizeof profile->ssid) ||
            !memchr(profile->psk, 0, sizeof profile->psk) ||
            !wifi_make_credential(profile->ssid, profile->psk, &checked, error) ||
            strcmp(checked.psk, profile->psk)) {
            wifi_error(error, "Invalid or duplicate derived system Wi-Fi credentials");
            return false;
        }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(profiles->items[j].ssid, profile->ssid)) {
                wifi_error(error, "Invalid or duplicate derived system Wi-Fi credentials");
                return false;
            }
    }
    int parent = -1;
    char name[NAME_MAX + 1], *original = NULL, *replacement = NULL;
    document_t document = {0};
    if (!open_parent(path, &parent, name, error))
        return false;
    bool ok =
        read_config(parent, name, &original, error) && parse_document(original, &document, error);
    if (!ok)
        goto done;
    replacement = render(&document, profiles);
    if (!replacement) {
        wifi_error(error, "Cannot allocate system Wi-Fi configuration");
        ok = false;
        goto done;
    }
    if (!replace_config(parent, name, replacement)) {
        bool restored = replace_config(parent, name, original);
        wifi_error(error,
                   restored
                       ? "Cannot commit system Wi-Fi configuration; original restored"
                       : "Cannot commit or restore system Wi-Fi configuration; restore its backup");
        ok = false;
        goto done;
    }
    if (!reconfigure(context)) {
        bool restored = replace_config(parent, name, original);
        if (restored)
            restored = reconfigure(context);
        wifi_error(error,
                   restored
                       ? "Supplicant rejected the update; original configuration restored"
                       : "System Wi-Fi rollback failed; restore its backup and restart supplicant");
        ok = false;
        goto done;
    }
    wifi_error(error, "");
done:
    close(parent);
    free(original);
    free(replacement);
    document_free(&document);
    return ok;
}

static bool valid_bssid(const char *value) {
    if (strlen(value) != 17 || !strcmp(value, "00:00:00:00:00:00"))
        return false;
    for (size_t i = 0; i < 17; ++i)
        if (i % 3 == 2 ? value[i] != ':' : !isxdigit((unsigned char)value[i]))
            return false;
    return true;
}

static void lowercase(char *text) {
    for (; *text; ++text)
        *text = (char)tolower((unsigned char)*text);
}

bool wifi_parse_dhcpcd_lease(const char *text, wifi_dhcpcd_lease_t *lease) {
    if (!text || !lease || strlen(text) > 2048)
        return false;
    char copy[2049];
    wifi_copy(copy, sizeof copy, text);
    const char *names[] = {"version", "generation", "bssid", "ipv4", "ssid_hex"};
    char *values[5] = {0}, *save = NULL;
    size_t count = 0;
    /* Empty interior records are invalid, as in the previous line parser. */
    if (strstr(text, "\n\n") || *text == '\n')
        return false;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *equal = strchr(line, '=');
        if (!equal)
            return false;
        *equal = '\0';
        size_t index = 0;
        while (index < 5 && strcmp(line, names[index]))
            ++index;
        if (index == 5 || values[index])
            return false;
        values[index] = equal + 1;
        ++count;
    }
    if ((count != 4 && count != 5) || !values[0] || strcmp(values[0], "1") || !values[1] ||
        !values[2] || !values[3] || !*values[1] || strlen(values[1]) > 80)
        return false;
    for (const unsigned char *p = (const unsigned char *)values[1]; *p; ++p)
        if (!isalnum(*p) && *p != '-' && *p != '_')
            return false;
    wifi_dhcpcd_lease_t parsed = {0};
    if (!wifi_copy(parsed.generation, sizeof parsed.generation, values[1]) ||
        !wifi_copy(parsed.bssid, sizeof parsed.bssid, values[2]) ||
        !wifi_copy(parsed.ipv4, sizeof parsed.ipv4, values[3]) || !valid_bssid(parsed.bssid))
        return false;
    lowercase(parsed.bssid);
    if (values[4]) {
        size_t length;
        if (!wifi_decode_hex(values[4], parsed.ssid, sizeof parsed.ssid, &length) ||
            length != strlen(parsed.ssid) || !wifi_valid_ssid(parsed.ssid))
            return false;
    }
    struct in_addr address;
    if (inet_pton(AF_INET, parsed.ipv4, &address) != 1)
        return false;
    uint32_t ip = ntohl(address.s_addr);
    if (!ip || ip == 0xc0a80401U || (ip >> 24) == 127 || (ip >> 16) == 0xa9fe || (ip >> 28) >= 14)
        return false;
    *lease = parsed;
    return true;
}

bool wifi_read_dhcpcd_lease(const char *directory, const char *interface_name,
                            wifi_dhcpcd_lease_t *lease) {
    if (!directory || directory[0] != '/' || !wifi_valid_interface(interface_name))
        return false;
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat status;
    if (fd < 0 || fstat(fd, &status) || status.st_uid != geteuid() ||
        (status.st_mode & 0777) != 0700) {
        if (fd >= 0)
            close(fd);
        return false;
    }
    char name[64];
    int length = snprintf(name, sizeof name, "%s.lease", interface_name);
    int record = length > 0 && (size_t)length < sizeof name
                     ? openat(fd, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)
                     : -1;
    close(fd);
    if (record < 0 || fstat(record, &status) || !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() || (status.st_mode & 0777) != 0600 || status.st_nlink != 1 ||
        status.st_size < 0 || status.st_size > 2048) {
        if (record >= 0)
            close(record);
        return false;
    }
    char bytes[2049];
    size_t used = 0;
    bool ok = true;
    while (used < sizeof bytes) {
        ssize_t count = read(record, bytes + used, sizeof bytes - used);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            ok = false;
            break;
        }
        if (!count)
            break;
        used += (size_t)count;
    }
    close(record);
    if (!ok || used > 2048 || memchr(bytes, 0, used))
        return false;
    bytes[used] = '\0';
    return wifi_parse_dhcpcd_lease(bytes, lease);
}

bool wifi_dhcpcd_lease_matches(const wifi_dhcpcd_lease_t *lease, const char *previous_generation,
                               const char *status, const char *const *addresses, size_t count) {
    if (!lease || !status || !*lease->generation ||
        (previous_generation && !strcmp(lease->generation, previous_generation)))
        return false;
    bool matched = false;
    for (size_t i = 0; addresses && i < count; ++i)
        if (addresses[i] && !strcmp(addresses[i], lease->ipv4))
            matched = true;
    if (!matched)
        return false;
    char state[32], encoded[256], ssid[WIFI_SSID_CAPACITY], bssid[18];
    if (!wifi_field(status, "wpa_state", state, sizeof state) || strcmp(state, "COMPLETED") ||
        !wifi_field(status, "ssid", encoded, sizeof encoded) ||
        !wifi_decode_wpa(encoded, ssid, sizeof ssid) || !wifi_valid_ssid(ssid) ||
        (*lease->ssid && strcmp(ssid, lease->ssid)) ||
        !wifi_field(status, "bssid", bssid, sizeof bssid))
        return false;
    lowercase(bssid);
    return !strcmp(bssid, lease->bssid);
}
