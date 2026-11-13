/* SPDX-License-Identifier: Apache-2.0 */
#include "device_identity.h"

#include <ctype.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int derive(const char *kind, const char *identity, char *output, size_t capacity) {
    /* Public, fixed namespace key; this digest is an identifier, not authentication. */
    static const char key[] = "mybot:RV1126B:device-id:v1";
    char input[64];
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (!output || capacity < 33 ||
        snprintf(input, sizeof(input), "%s:%s", kind, identity) >= (int)sizeof(input))
        return -1;
    if (!HMAC(EVP_sha256(), key, (int)strlen(key), (const unsigned char *)input, strlen(input),
              digest, &length) ||
        length < 12)
        return -1;
    static const char hex[] = "0123456789ABCDEF";
    memcpy(output, "RV1126B-", 8);
    for (unsigned int i = 0; i < 12; ++i) {
        output[8 + i * 2] = hex[digest[i] >> 4];
        output[9 + i * 2] = hex[digest[i] & 15];
    }
    output[32] = '\0';
    return 0;
}

int rv1126b_identity_from_serial(FILE *cpuinfo, char *output, size_t capacity) {
    if (!cpuinfo || !output || capacity < 33)
        return -1;
    char line[512], serial[17] = {0};
    bool found = false;
    while (fgets(line, sizeof(line), cpuinfo)) {
        if (!strchr(line, '\n') && !feof(cpuinfo))
            return -1;
        char *key = line;
        while (isspace((unsigned char)*key))
            ++key;
        char *colon = strchr(key, ':');
        if (!colon)
            continue;
        char *end = colon;
        while (end > key && isspace((unsigned char)end[-1]))
            --end;
        *end = '\0';
        if (strcmp(key, "Serial"))
            continue;
        char *value = colon + 1;
        while (isspace((unsigned char)*value))
            ++value;
        end = value + strlen(value);
        while (end > value && isspace((unsigned char)end[-1]))
            --end;
        if (found || end - value != 16)
            return -1;
        for (int i = 0; i < 16; ++i) {
            if (!isxdigit((unsigned char)value[i]))
                return -1;
            serial[i] = (char)toupper((unsigned char)value[i]);
        }
        if (!strcmp(serial, "0000000000000000") || !strcmp(serial, "FFFFFFFFFFFFFFFF"))
            return -1;
        found = true;
    }
    if (!found || ferror(cpuinfo))
        return -1;
    return derive("cpu-serial", serial, output, capacity);
}

int rv1126b_identity_from_mac(const char *interface_name, char *output, size_t capacity) {
    if (!interface_name || !interface_name[0] || strlen(interface_name) > 15)
        return -1;
    for (const char *p = interface_name; *p; ++p)
        if (!isalnum((unsigned char)*p) && !strchr("_.-", *p))
            return -1;
    char path[128], mac[32], extra;
    snprintf(path, sizeof(path), "/sys/class/net/%s/addr_assign_type", interface_name);
    FILE *file = fopen(path, "r");
    if (!file)
        return -1;
    unsigned int assigned = 1;
    int read = fscanf(file, "%u", &assigned);
    int closed = fclose(file);
    if (read != 1 || closed || assigned != 0)
        return -1;
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", interface_name);
    file = fopen(path, "r");
    if (!file)
        return -1;
    char *line = fgets(mac, sizeof(mac), file);
    closed = fclose(file);
    if (!line || closed)
        return -1;
    unsigned int bytes[6];
    int count = sscanf(mac, "%2x:%2x:%2x:%2x:%2x:%2x %c", &bytes[0], &bytes[1], &bytes[2],
                       &bytes[3], &bytes[4], &bytes[5], &extra);
    if (count != 6 || (bytes[0] & 1) ||
        !(bytes[0] | bytes[1] | bytes[2] | bytes[3] | bytes[4] | bytes[5]))
        return -1;
    snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X", bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5]);
    return derive("factory-mac", mac, output, capacity);
}
