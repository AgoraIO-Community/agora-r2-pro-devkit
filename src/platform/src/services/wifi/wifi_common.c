/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "wifi_common.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void wifi_buffer_init(wifi_buffer_t *buffer, size_t limit) {
    *buffer = (wifi_buffer_t){.limit = limit};
}

void wifi_buffer_free(wifi_buffer_t *buffer) {
    if (!buffer)
        return;
    free(buffer->data);
    *buffer = (wifi_buffer_t){0};
}

static bool reserve(wifi_buffer_t *buffer, size_t additional) {
    if (!buffer || buffer->length > buffer->limit || additional > buffer->limit - buffer->length ||
        buffer->length + additional == SIZE_MAX)
        return false;
    size_t needed = buffer->length + additional + 1;
    if (needed <= buffer->capacity)
        return true;
    size_t capacity = buffer->capacity ? buffer->capacity : 128;
    while (capacity < needed && capacity <= SIZE_MAX / 2)
        capacity *= 2;
    if (capacity < needed)
        capacity = needed;
    if (buffer->limit < SIZE_MAX && capacity > buffer->limit + 1)
        capacity = buffer->limit + 1;
    char *replacement = realloc(buffer->data, capacity);
    if (!replacement)
        return false;
    buffer->data = replacement;
    buffer->capacity = capacity;
    buffer->data[buffer->length] = '\0';
    return true;
}

bool wifi_buffer_append(wifi_buffer_t *buffer, const void *bytes, size_t length) {
    if (!bytes && length)
        return false;
    size_t offset = 0;
    bool internal = buffer && buffer->data && (uintptr_t)bytes >= (uintptr_t)buffer->data &&
                    (uintptr_t)bytes < (uintptr_t)buffer->data + buffer->length;
    if (internal) {
        offset = (size_t)((const char *)bytes - buffer->data);
        if (length > buffer->length - offset)
            return false;
    }
    if (!reserve(buffer, length))
        return false;
    if (length)
        memmove(buffer->data + buffer->length, internal ? buffer->data + offset : bytes, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

bool wifi_buffer_text(wifi_buffer_t *buffer, const char *text) {
    return text && wifi_buffer_append(buffer, text, strlen(text));
}

bool wifi_buffer_printf(wifi_buffer_t *buffer, const char *format, ...) {
    if (!format)
        return false;
    va_list args, measure;
    va_start(args, format);
    va_copy(measure, args);
    int length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    bool ok = length >= 0 && reserve(buffer, (size_t)length);
    if (ok) {
        int written = vsnprintf(buffer->data + buffer->length, (size_t)length + 1, format, args);
        ok = written == length;
        if (ok)
            buffer->length += (size_t)length;
        buffer->data[buffer->length] = '\0';
    }
    va_end(args);
    return ok;
}

char *wifi_buffer_take(wifi_buffer_t *buffer) {
    if (!reserve(buffer, 0))
        return NULL;
    char *result = buffer->data;
    *buffer = (wifi_buffer_t){0};
    return result;
}

bool wifi_copy(char *output, size_t capacity, const char *text) {
    if (!output || !capacity)
        return false;
    if (!text)
        text = "";
    size_t length = strlen(text), used = length < capacity ? length : capacity - 1;
    memmove(output, text, used);
    output[used] = '\0';
    return length < capacity;
}

void wifi_error(char *output, const char *message) {
    if (output)
        wifi_copy(output, WIFI_ERROR_CAPACITY, message);
}

bool wifi_cancelled(const wifi_cancel_t *cancel) {
    return cancel && cancel->requested && cancel->requested(cancel->context);
}

uint64_t wifi_now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

void wifi_sleep_ms(unsigned milliseconds) {
    struct timespec delay = {.tv_sec = milliseconds / 1000,
                             .tv_nsec = (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&delay, &delay) && errno == EINTR) {
    }
}

bool wifi_field(const char *text, const char *key, char *output, size_t capacity) {
    if (!text || !key || !output || !capacity)
        return false;
    output[0] = '\0';
    size_t key_length = strlen(key);
    bool found = false;
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        size_t length = (size_t)(end - line);
        if (length > key_length && !memcmp(line, key, key_length) && line[key_length] == '=') {
            size_t used = length - key_length - 1;
            if (used && line[length - 1] == '\r')
                --used;
            if (found || used >= capacity) {
                output[0] = '\0';
                return false;
            }
            memcpy(output, line + key_length + 1, used);
            output[used] = '\0';
            found = true;
        }
        line = *end ? end + 1 : end;
    }
    return found;
}

bool wifi_utf8_valid(const char *text, size_t length) {
    if (!text && length)
        return false;
    for (size_t i = 0; i < length;) {
        unsigned char c = (unsigned char)text[i++];
        if (c < 0x80)
            continue;
        unsigned count = c >= 0xc2 && c <= 0xdf   ? 1
                         : c >= 0xe0 && c <= 0xef ? 2
                         : c >= 0xf0 && c <= 0xf4 ? 3
                                                  : 0;
        if (!count || count > length - i)
            return false;
        uint32_t point = c & ((1u << (6 - count)) - 1);
        for (unsigned n = 0; n < count; ++n) {
            unsigned char next = (unsigned char)text[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            point = (point << 6) | (next & 0x3f);
        }
        if ((count == 1 && point < 0x80) || (count == 2 && point < 0x800) ||
            (count == 3 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff))
            return false;
    }
    return true;
}
