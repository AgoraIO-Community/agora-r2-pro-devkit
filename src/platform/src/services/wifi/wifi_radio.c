/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "wifi_backend_internal.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wifi_frequency_channel(int frequency) {
    if (frequency == 2484)
        return 14;
    if (frequency >= 2412 && frequency <= 2472 && !((frequency - 2407) % 5))
        return (frequency - 2407) / 5;
    if (frequency >= 5000 && frequency <= 5895 && !(frequency % 5))
        return (frequency - 5000) / 5;
    if (frequency >= 5955 && frequency <= 7115 && !((frequency - 5950) % 5))
        return (frequency - 5950) / 5;
    return 0;
}

bool wifi_wiphy_from_interface_info(const char *text, char *output, size_t capacity) {
    char *copy = strdup(text), *context = NULL;
    if (!copy)
        return false;
    bool ok = false;
    for (char *line = strtok_r(copy, "\n", &context); line; line = strtok_r(NULL, "\n", &context)) {
        char key[32], number[32], extra[2];
        if (sscanf(line, "%31s %31s %1s", key, number, extra) != 2 || strcmp(key, "wiphy") ||
            !*number || strlen(number) > 8)
            continue;
        bool digits = true;
        for (const unsigned char *p = (unsigned char *)number; *p; ++p)
            digits &= isdigit(*p) != 0;
        if (digits) {
            int used = snprintf(output, capacity, "phy%s", number);
            ok = used >= 0 && (size_t)used < capacity;
            break;
        }
    }
    free(copy);
    if (!ok && capacity)
        output[0] = 0;
    return ok;
}

static long combination_limit(const char *value) {
    while (isspace((unsigned char)*value))
        ++value;
    if (strncmp(value, "<=", 2))
        return 0;
    value += 2;
    while (isspace((unsigned char)*value))
        ++value;
    char *end;
    errno = 0;
    long count = strtol(value, &end, 10);
    return errno || end == value || count < 0 || count > 1000 ? 0 : count;
}

static bool combination_supports(const char *text) {
    const char *channels = strstr(text, "#channels"), *total = strstr(text, "total");
    if (!channels || !total || combination_limit(channels + 9) < 2 ||
        combination_limit(total + 5) < 2)
        return false;
    bool managed = false, ap = false, shared = false;
    const char *position = text;
    while ((position = strstr(position, "#{"))) {
        const char *end = strchr(position + 2, '}');
        if (!end)
            return false;
        size_t length = (size_t)(end - position - 2);
        char modes[256];
        if (length >= sizeof(modes))
            return false;
        memcpy(modes, position + 2, length);
        modes[length] = 0;
        bool group_managed = false, group_ap = false;
        char *context = NULL;
        for (char *mode = strtok_r(modes, ",", &context); mode;
             mode = strtok_r(NULL, ",", &context)) {
            char compact[128];
            size_t used = 0;
            for (size_t i = 0; mode[i]; ++i)
                if (!isspace((unsigned char)mode[i])) {
                    if (used + 1 >= sizeof(compact))
                        return false;
                    compact[used++] = mode[i];
                }
            compact[used] = 0;
            group_managed |= !strcmp(compact, "managed");
            group_ap |= !strcmp(compact, "AP");
        }
        long maximum = combination_limit(end + 1);
        shared |= group_managed && group_ap && maximum >= 2;
        managed |= group_managed && !group_ap && maximum >= 1;
        ap |= group_ap && !group_managed && maximum >= 1;
        position = end + 1;
    }
    return shared || (managed && ap);
}

bool wifi_managed_ap_multiple_channels(const char *text) {
    char *copy = strdup(text), *context = NULL;
    if (!copy)
        return false;
    bool section = false, supported = false;
    wifi_buffer_t combination;
    wifi_buffer_init(&combination, 65536);
    for (char *line = strtok_r(copy, "\n", &context); line; line = strtok_r(NULL, "\n", &context)) {
        if (strstr(line, "valid interface combinations:")) {
            section = true;
            continue;
        }
        if (!section)
            continue;
        const char *first = line;
        while (*first == ' ' || *first == '\t')
            ++first;
        if (*first == '*') {
            if (combination.data && combination_supports(combination.data)) {
                supported = true;
                break;
            }
            combination.length = 0;
            if (combination.data)
                combination.data[0] = 0;
            if (!wifi_buffer_text(&combination, first + 1))
                break;
        } else if (!wifi_buffer_text(&combination, " ") || !wifi_buffer_text(&combination, line))
            break;
    }
    supported |= combination.data && combination_supports(combination.data);
    wifi_buffer_free(&combination);
    free(copy);
    return supported;
}

bool wifi_select_ap_radio(int frequency, bool multiple, wifi_ap_radio_plan_t *plan, char *error) {
    wifi_ap_radio_plan_t selected = {'g', 1};
    if (!frequency) {
        *plan = selected;
        wifi_error(error, "");
        return true;
    }
    int channel = wifi_frequency_channel(frequency);
    if (frequency >= 2412 && frequency <= 2484 && channel >= 1 && channel <= 14) {
        selected.channel = channel;
        *plan = selected;
        wifi_error(error, "");
        return true;
    }
    if (frequency >= 5000 && frequency <= 7115) {
        if (multiple) {
            *plan = selected;
            wifi_error(error, "");
            return true;
        }
        if (frequency < 5955 && ((channel >= 36 && channel <= 48 && !(channel % 4)) ||
                                 (channel >= 149 && channel <= 165 && !((channel - 149) % 4)))) {
            selected.hw_mode = 'a';
            selected.channel = channel;
            *plan = selected;
            wifi_error(error, "");
            return true;
        }
        wifi_error(error, frequency >= 5955
                              ? "6 GHz station requires a verified multi-channel AP+STA combination"
                              : "Single-channel AP+STA requires a non-DFS 5 GHz station channel");
        return false;
    }
    wifi_error(error, "Cannot select a safe AP channel for the current station frequency");
    return false;
}

bool wifi_network_id_for_ssid(const char *list, const char *ssid, char output[9]) {
    char *copy = strdup(list), *context = NULL;
    output[0] = 0;
    if (!copy)
        return false;
    bool found = false;
    for (char *line = strtok_r(copy, "\n", &context); line; line = strtok_r(NULL, "\n", &context)) {
        char *first = strchr(line, '\t'), *second = first ? strchr(first + 1, '\t') : NULL;
        if (!first || !second || first == line || first - line > 8)
            continue;
        bool digits = true;
        for (char *p = line; p < first; ++p)
            digits &= isdigit((unsigned char)*p) != 0;
        if (!digits)
            continue;
        *first = 0;
        *second = 0;
        char decoded[WIFI_SSID_CAPACITY];
        if (wifi_decode_wpa(first + 1, decoded, sizeof(decoded)) && !strcmp(decoded, ssid)) {
            wifi_copy(output, 9, line);
            found = true;
            break;
        }
    }
    free(copy);
    return found;
}

static bool field_equal(const char *text, const char *key, const char *expected) {
    char value[256];
    return wifi_field(text, key, value, sizeof(value)) && !strcmp(value, expected);
}

static bool normalized_bssid(const char *text, char result[18]) {
    if (strlen(text) != 17)
        return false;
    for (size_t i = 0; i < 17; ++i) {
        if ((i % 3 == 2 && text[i] != ':') || (i % 3 != 2 && !isxdigit((unsigned char)text[i])))
            return false;
        result[i] = (char)tolower((unsigned char)text[i]);
    }
    result[17] = 0;
    return strcmp(result, "00:00:00:00:00:00") != 0;
}

bool wifi_same_association_lease(const char *initial, const char *current, const char *ssid,
                                 const char *trial, const char *initial_route,
                                 const char *current_route) {
    if (!*initial_route || strcmp(initial_route, current_route) || !*trial ||
        !field_equal(initial, "wpa_state", "COMPLETED") ||
        !field_equal(current, "wpa_state", "COMPLETED") || !field_equal(current, "id", trial))
        return false;
    char first[256], second[256], decoded[WIFI_SSID_CAPACITY];
    if (!wifi_field(initial, "ssid", first, sizeof(first)) ||
        !wifi_decode_wpa(first, decoded, sizeof(decoded)) || strcmp(decoded, ssid) ||
        !wifi_field(current, "ssid", second, sizeof(second)) ||
        !wifi_decode_wpa(second, decoded, sizeof(decoded)) || strcmp(decoded, ssid))
        return false;
    char initial_bssid[18], current_bssid[18];
    return wifi_field(initial, "bssid", first, sizeof(first)) &&
           normalized_bssid(first, initial_bssid) &&
           wifi_field(current, "bssid", second, sizeof(second)) &&
           normalized_bssid(second, current_bssid) && !strcmp(initial_bssid, current_bssid);
}

bool wifi_ensure_committed_selection(const char *status, const char *networks, const char *ssid,
                                     bool (*send)(void *, const char *), void *context,
                                     bool *reassociated, char *error) {
    *reassociated = false;
    char id[9];
    if (!wifi_network_id_for_ssid(networks, ssid, id) || !send) {
        wifi_error(error, "Committed Wi-Fi profile is missing from its supplicant");
        return false;
    }
    char name[256], decoded[WIFI_SSID_CAPACITY], state[64];
    bool progress = wifi_field(status, "wpa_state", state, sizeof(state)) &&
                    (!strcmp(state, "COMPLETED") || !strcmp(state, "ASSOCIATING") ||
                     !strcmp(state, "ASSOCIATED") || !strcmp(state, "4WAY_HANDSHAKE") ||
                     !strcmp(state, "GROUP_HANDSHAKE"));
    if (field_equal(status, "id", id) && wifi_field(status, "ssid", name, sizeof(name)) &&
        wifi_decode_wpa(name, decoded, sizeof(decoded)) && !strcmp(decoded, ssid) && progress) {
        wifi_error(error, "");
        return true;
    }
    char command[64];
    snprintf(command, sizeof(command), "ENABLE_NETWORK %s", id);
    if (!send(context, command) || !send(context, "REASSOCIATE")) {
        wifi_error(error, "Supplicant could not select the committed Wi-Fi profile");
        return false;
    }
    *reassociated = true;
    wifi_error(error, "");
    return true;
}

static int compare_access_points(const void *left, const void *right) {
    const wifi_access_point_t *a = left, *b = right;
    return b->rssi - a->rssi;
}

void wifi_parse_scan_results(const char *text, wifi_scan_t *scan) {
    wifi_scan_t result = {0};
    char *copy = strdup(text), *context = NULL;
    if (!copy) {
        *scan = result;
        return;
    }
    char *line = strtok_r(copy, "\n", &context);
    while (line && (line = strtok_r(NULL, "\n", &context))) {
        char *columns[5];
        columns[0] = line;
        size_t count = 1;
        while (count < 5) {
            char *tab = strchr(columns[count - 1], '\t');
            if (!tab)
                break;
            *tab = 0;
            columns[count++] = tab + 1;
        }
        if (count != 5)
            continue;
        wifi_access_point_t item = {{0}, 0, 0, 0};
        if (!wifi_decode_wpa(columns[4], item.ssid, sizeof(item.ssid)) ||
            !wifi_valid_ssid(item.ssid))
            continue;
        char *end;
        errno = 0;
        long frequency = strtol(columns[1], &end, 10);
        if (errno || end == columns[1] || *end || frequency < 0 || frequency > 10000)
            continue;
        long rssi = strtol(columns[2], &end, 10);
        if (errno || end == columns[2] || *end || rssi < -150 || rssi > 0)
            continue;
        item.rssi = (int)rssi;
        item.channel = wifi_frequency_channel((int)frequency);
        const char *flags = columns[3];
        if (strstr(flags, "SAE"))
            item.authmode = strstr(flags, "PSK") ? 7 : 6;
        else if (strstr(flags, "EAP"))
            item.authmode = 5;
        else if (strstr(flags, "WPA2") || strstr(flags, "RSN"))
            item.authmode = strstr(flags, "[WPA-") ? 4 : 3;
        else if (strstr(flags, "WPA"))
            item.authmode = 2;
        else if (strstr(flags, "WEP"))
            item.authmode = 1;
        size_t duplicate = result.count;
        for (size_t i = 0; i < result.count; ++i)
            if (!strcmp(result.items[i].ssid, item.ssid)) {
                duplicate = i;
                break;
            }
        if (duplicate < result.count) {
            if (result.items[duplicate].rssi < item.rssi)
                result.items[duplicate] = item;
        } else if (result.count < WIFI_MAX_ACCESS_POINTS)
            result.items[result.count++] = item;
        else {
            size_t weakest = 0;
            for (size_t i = 1; i < result.count; ++i)
                if (result.items[i].rssi < result.items[weakest].rssi)
                    weakest = i;
            if (result.items[weakest].rssi < item.rssi)
                result.items[weakest] = item;
        }
    }
    free(copy);
    qsort(result.items, result.count, sizeof(result.items[0]), compare_access_points);
    *scan = result;
}
