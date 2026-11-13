/* SPDX-License-Identifier: Apache-2.0 */
#include "device_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *const s_keys[BOTSTATION_CONFIG_KEYS] = {
    "BOTSTATION_ENABLED",
    "BOTSTATION_SERVER",
    "BOTSTATION_DEVICE_ID",
    "BOTSTATION_FIRMWARE_VERSION",
    "BOTSTATION_HARDWARE_MODEL",
    "BOTSTATION_CAPTURE_DEVICE",
    "BOTSTATION_PLAYBACK_DEVICE",
    "BOTSTATION_INTERFACE",
    "BOTSTATION_INPUT_DEVICE",
    "BOTSTATION_TALK_KEY",
    "MYBOT_KV_STORE_DIR",
    "MYBOT_ASSETS_DIR",
    "MYBOT_LOCALE",
    "MYBOT_LCD_COLOR",
    "BOTSTATION_VIDEO_DEVICE",
};

static int key_index(const char *key) {
    for (int i = 0; i < BOTSTATION_CONFIG_KEYS; ++i) {
        if (strcmp(key, s_keys[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static char *skip_space(char *text) {
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

int botstation_device_id_read(FILE *file, char *device_id, size_t size) {
    char line[1024];
    char serial[17] = {0};
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        if (!strchr(line, '\n') && !feof(file)) {
            fprintf(stderr, "CPU info line is too long\n");
            return -1;
        }
        char *key = skip_space(line);
        char *colon = strchr(key, ':');
        if (!colon) {
            continue;
        }
        char *end = colon;
        while (end > key && isspace((unsigned char)end[-1])) {
            --end;
        }
        *end = '\0';
        if (strcmp(key, "Serial") != 0) {
            continue;
        }
        char *value = skip_space(colon + 1);
        end = value + strlen(value);
        while (end > value && isspace((unsigned char)end[-1])) {
            --end;
        }
        if (found || end - value != 16) {
            fprintf(stderr, "CPU Serial must be a single 16-digit hexadecimal value\n");
            return -1;
        }
        bool nonzero = false;
        for (size_t i = 0; i < 16; ++i) {
            unsigned char digit = (unsigned char)value[i];
            if (!isxdigit(digit)) {
                fprintf(stderr, "CPU Serial contains a non-hexadecimal digit\n");
                return -1;
            }
            serial[i] = (char)toupper(digit);
            nonzero = nonzero || digit != '0';
        }
        if (!nonzero || strcmp(serial, "FFFFFFFFFFFFFFFF") == 0) {
            fprintf(stderr, "CPU Serial is an invalid placeholder\n");
            return -1;
        }
        found = true;
    }
    if (ferror(file)) {
        fprintf(stderr, "Cannot read CPU info: %s\n", strerror(errno));
        return -1;
    }
    if (!found) {
        fprintf(stderr, "CPU info has no Serial; configure a unique BOTSTATION_DEVICE_ID\n");
        return -1;
    }
    if (size < sizeof("RV1126B-") + 16) {
        fprintf(stderr, "Device ID buffer is too small\n");
        return -1;
    }
    snprintf(device_id, size, "RV1126B-%s", serial);
    return 0;
}

static int parse_line(char *line, botstation_file_config_t *config) {
    char *key = skip_space(line);
    if (!*key || *key == '#') {
        return 0;
    }
    char *equals = strchr(key, '=');
    if (!equals) {
        return -1;
    }
    char *end = equals;
    while (end > key && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    int index = key_index(key);
    if (index < 0) {
        return -1;
    }
    char *value = skip_space(equals + 1);
    char *tail;
    if (*value == '\'' || *value == '"') {
        char quote = *value++;
        end = strchr(value, quote);
        if (!end) {
            return -1;
        }
        tail = end + 1;
        *end = '\0';
        /* Configuration is data, never a shell program or variable expansion. */
        if (quote == '"' && strpbrk(value, "$`\\") != NULL) {
            return -1;
        }
    } else {
        end = value;
        while (*end && !isspace((unsigned char)*end) && *end != '#') {
            if (!isalnum((unsigned char)*end) && !strchr("_-./:,", *end)) {
                return -1;
            }
            ++end;
        }
        tail = end;
        if (*end == '#') {
            *end = '\0';
        } else if (*end) {
            *end = '\0';
            tail = end + 1;
        }
    }
    tail = skip_space(tail);
    if ((*tail && *tail != '#') || strlen(value) >= BOTSTATION_CONFIG_VALUE_SIZE) {
        return -1;
    }
    memcpy(config->values[index], value, strlen(value) + 1);
    config->present[index] = true;
    return 0;
}

int botstation_config_read(FILE *file, const char *path, botstation_file_config_t *config) {
    char line[1024];
    unsigned int number = 0;
    memset(config, 0, sizeof(*config));
    while (fgets(line, sizeof(line), file)) {
        ++number;
        if ((!strchr(line, '\n') && !feof(file)) || parse_line(line, config) < 0) {
            fprintf(stderr,
                    "%s:%u: invalid configuration; expected a known KEY=VALUE "
                    "with a literal value (no shell commands)\n",
                    path, number);
            return -1;
        }
    }
    if (ferror(file)) {
        fprintf(stderr, "Cannot read %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int botstation_config_load(const char *path, botstation_file_config_t *config) {
    memset(config, 0, sizeof(*config));
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) {
            return 0; /* Command-line-only deployments need no installed config file. */
        }
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    int result = botstation_config_read(file, path, config);
    if (fclose(file) != 0) {
        fprintf(stderr, "Cannot close %s: %s\n", path, strerror(errno));
        return -1;
    }
    return result;
}

const char *botstation_config_get(const botstation_file_config_t *config, const char *key,
                                  const char *fallback) {
    int index = key_index(key);
    return index >= 0 && config->present[index] ? config->values[index] : fallback;
}
