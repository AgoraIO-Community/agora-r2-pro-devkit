/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "wifi_backend_internal.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool wifi_valid_interface(const char *name) {
    if (!name || !*name || strlen(name) >= IFNAMSIZ)
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.')
            return false;
    return true;
}

bool wifi_valid_ssid(const char *ssid) {
    if (!ssid || !*ssid || strlen(ssid) > 32 || !wifi_utf8_valid(ssid, strlen(ssid)))
        return false;
    const unsigned char *bytes = (const unsigned char *)ssid;
    for (size_t i = 0; bytes[i]; ++i) {
        if (bytes[i] < 0x20 || bytes[i] == 0x7f)
            return false;
        if (bytes[i] == 0xc2 && bytes[i + 1] >= 0x80 && bytes[i + 1] <= 0x9f)
            return false;
    }
    return true;
}

bool wifi_hex_psk(const char *psk) {
    if (!psk || strlen(psk) != 64)
        return false;
    for (const unsigned char *p = (const unsigned char *)psk; *p; ++p)
        if (!isxdigit(*p))
            return false;
    return true;
}

bool wifi_encode_hex(const void *bytes, size_t length, char *output, size_t capacity) {
    static const char digits[] = "0123456789abcdef";
    if (length > (SIZE_MAX - 1) / 2 || capacity < length * 2 + 1)
        return false;
    const unsigned char *input = bytes;
    for (size_t i = 0; i < length; ++i) {
        output[i * 2] = digits[input[i] >> 4];
        output[i * 2 + 1] = digits[input[i] & 15];
    }
    output[length * 2] = 0;
    return true;
}

static int nibble(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool wifi_decode_hex(const char *text, char *output, size_t capacity, size_t *length) {
    size_t size = strlen(text);
    if (size % 2 || capacity <= size / 2)
        return false;
    for (size_t i = 0; i < size; i += 2) {
        int first = nibble((unsigned char)text[i]), second = nibble((unsigned char)text[i + 1]);
        if (first < 0 || second < 0)
            return false;
        output[i / 2] = (char)((first << 4) | second);
    }
    output[size / 2] = 0;
    if (length)
        *length = size / 2;
    return true;
}

bool wifi_decode_wpa(const char *text, char *output, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\\') {
            c = (unsigned char)text[++i];
            switch (c) {
            case '\\':
            case '"':
                break;
            case 'e':
                c = 0x1b;
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'x': {
                if (!text[i + 1] || !text[i + 2])
                    return false;
                int first = nibble((unsigned char)text[i + 1]),
                    second = nibble((unsigned char)text[i + 2]);
                if (first < 0 || second < 0)
                    return false;
                c = (unsigned char)((first << 4) | second);
                i += 2;
                break;
            }
            default:
                return false;
            }
        }
        if (!c || used + 1 >= capacity)
            return false;
        output[used++] = (char)c;
    }
    if (!capacity)
        return false;
    output[used] = 0;
    return true;
}

bool wifi_make_credential(const char *ssid, const char *password, wifi_credential_t *credential,
                          char *error) {
    if (!wifi_valid_ssid(ssid)) {
        wifi_error(error, "SSID must contain 1–32 UTF-8 bytes without control characters");
        return false;
    }
    if (!password || !credential) {
        wifi_error(error, "Invalid Wi-Fi credentials");
        return false;
    }
    wifi_credential_t result = {{0}, {0}};
    wifi_copy(result.ssid, sizeof(result.ssid), ssid);
    if (wifi_hex_psk(password)) {
        for (size_t i = 0; i < 64; ++i)
            result.psk[i] = (char)tolower((unsigned char)password[i]);
    } else if (*password) {
        size_t length = strlen(password);
        bool valid = length >= 8 && length <= 63;
        for (size_t i = 0; i < length; ++i)
            valid &= (unsigned char)password[i] >= 0x20 && password[i] != 0x7f;
        if (!valid) {
            wifi_error(error,
                       "Password must contain 8–63 bytes, a 64-digit hexadecimal PSK, or be empty");
            return false;
        }
        unsigned char key[32];
        if (PKCS5_PBKDF2_HMAC_SHA1(password, (int)length, (const unsigned char *)ssid,
                                   (int)strlen(ssid), 4096, sizeof(key), key) != 1) {
            wifi_error(error, "Cannot derive Wi-Fi key");
            return false;
        }
        wifi_encode_hex(key, sizeof(key), result.psk, sizeof(result.psk));
        OPENSSL_cleanse(key, sizeof(key));
    }
    *credential = result;
    wifi_error(error, "");
    return true;
}

bool wifi_private_directory(const char *path, int *descriptor, char *error) {
    *descriptor = -1;
    if (!path || !*path || (mkdir(path, 0700) && errno != EEXIST)) {
        wifi_error(error, "Cannot create private Wi-Fi directory");
        return false;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0077)) {
        if (fd >= 0)
            close(fd);
        wifi_error(error, "Wi-Fi directory must be owned by the current user with mode 0700");
        return false;
    }
    *descriptor = fd;
    return true;
}

static bool write_all(int fd, const char *contents) {
    size_t size = strlen(contents), offset = 0;
    while (offset < size) {
        ssize_t used = write(fd, contents + offset, size - offset);
        if (used < 0 && errno == EINTR)
            continue;
        if (used <= 0)
            return false;
        offset += (size_t)used;
    }
    return true;
}

bool wifi_atomic_private_file(int directory, const char *name, const char *contents, char *error) {
    struct stat st;
    if (!fstatat(directory, name, &st, AT_SYMLINK_NOFOLLOW) &&
        (!S_ISREG(st.st_mode) || st.st_uid != geteuid())) {
        wifi_error(error, "Refusing unsafe Wi-Fi state file");
        return false;
    }
    char temporary[128];
    int size = snprintf(temporary, sizeof(temporary), "%s.new-%ld", name, (long)getpid());
    if (size < 0 || (size_t)size >= sizeof(temporary))
        return false;
    int fd =
        openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        wifi_error(error, "Cannot create private Wi-Fi state file");
        return false;
    }
    bool ok = write_all(fd, contents) && !fsync(fd);
    if (close(fd))
        ok = false;
    if (ok)
        ok = !renameat(directory, temporary, directory, name);
    if (!ok) {
        unlinkat(directory, temporary, 0);
        wifi_error(error, "Cannot atomically save Wi-Fi state");
        return false;
    }
    if (fsync(directory)) {
        wifi_error(error, "Cannot synchronize Wi-Fi state directory");
        return false;
    }
    return true;
}

static bool read_private(int directory, wifi_buffer_t *contents, char *error) {
    int fd = openat(directory, "wifi.credentials", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 && errno == ENOENT)
        return true;
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0077) || st.st_size < 0 || st.st_size > 16384) {
        if (fd >= 0)
            close(fd);
        wifi_error(
            error,
            "Cannot read private Wi-Fi credentials (regular owned file, mode 0600 required)");
        return false;
    }
    bool ok = true;
    char bytes[2048];
    for (;;) {
        ssize_t used = read(fd, bytes, sizeof(bytes));
        if (used < 0 && errno == EINTR)
            continue;
        if (used < 0) {
            ok = false;
            break;
        }
        if (!used)
            break;
        if (memchr(bytes, 0, (size_t)used) || !wifi_buffer_append(contents, bytes, (size_t)used)) {
            ok = false;
            break;
        }
    }
    close(fd);
    if (!ok)
        wifi_error(error, "Cannot read Wi-Fi state");
    return ok;
}

bool wifi_read_credentials(int directory, wifi_credentials_t *credentials, char *error) {
    wifi_buffer_t contents;
    wifi_buffer_init(&contents, 16384);
    if (!read_private(directory, &contents, error)) {
        wifi_buffer_free(&contents);
        return false;
    }
    wifi_credentials_t result = {0};
    if (!contents.length) {
        *credentials = result;
        wifi_buffer_free(&contents);
        wifi_error(error, "");
        return true;
    }
    char *line = contents.data, *newline = strchr(line, '\n');
    if (newline)
        *newline++ = 0;
    bool ok = !strcmp(line, "MYBOT_WIFI_V1");
    line = newline;
    while (ok && line && *line) {
        newline = strchr(line, '\n');
        if (newline)
            *newline++ = 0;
        char *tab = strchr(line, '\t');
        wifi_credential_t item = {{0}, {0}};
        size_t decoded = 0;
        if (!tab) {
            ok = false;
            break;
        }
        *tab++ = 0;
        ok = wifi_decode_hex(line, item.ssid, sizeof(item.ssid), &decoded) &&
             strlen(item.ssid) == decoded && wifi_valid_ssid(item.ssid) &&
             wifi_copy(item.psk, sizeof(item.psk), tab) && (!*item.psk || wifi_hex_psk(item.psk)) &&
             result.count < WIFI_MAX_NETWORKS;
        for (size_t i = 0; ok && i < result.count; ++i)
            ok &= strcmp(result.items[i].ssid, item.ssid) != 0;
        if (ok)
            result.items[result.count++] = item;
        line = newline;
    }
    wifi_buffer_free(&contents);
    if (!ok) {
        wifi_error(error, "Invalid or excessive Wi-Fi credential records");
        return false;
    }
    *credentials = result;
    wifi_error(error, "");
    return true;
}

bool wifi_write_credentials(int directory, const wifi_credentials_t *credentials, char *error) {
    if (credentials->count > WIFI_MAX_NETWORKS) {
        wifi_error(error, "At most 10 saved Wi-Fi networks are supported");
        return false;
    }
    wifi_buffer_t contents;
    wifi_buffer_init(&contents, 16384);
    bool ok = wifi_buffer_text(&contents, "MYBOT_WIFI_V1\n");
    for (size_t i = 0; ok && i < credentials->count; ++i) {
        const wifi_credential_t *item = &credentials->items[i];
        char encoded[65];
        ok = memchr(item->ssid, 0, sizeof(item->ssid)) && memchr(item->psk, 0, sizeof(item->psk)) &&
             wifi_valid_ssid(item->ssid) && (!*item->psk || wifi_hex_psk(item->psk));
        for (size_t j = 0; ok && j < i; ++j)
            ok &= strcmp(item->ssid, credentials->items[j].ssid) != 0;
        if (ok)
            ok = wifi_encode_hex(item->ssid, strlen(item->ssid), encoded, sizeof(encoded)) &&
                 wifi_buffer_printf(&contents, "%s\t%s\n", encoded, item->psk);
    }
    if (!ok)
        wifi_error(error, "Invalid or duplicate Wi-Fi credentials");
    else
        ok = wifi_atomic_private_file(directory, "wifi.credentials", contents.data, error);
    wifi_buffer_free(&contents);
    return ok;
}
