/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_WIFI_COMMON_H
#define RV1126B_WIFI_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_CAPACITY 33
#define WIFI_PSK_CAPACITY 65
#define WIFI_ERROR_CAPACITY 160
#define WIFI_MAX_NETWORKS 10
#define WIFI_MAX_ACCESS_POINTS 64

typedef struct {
    char ssid[WIFI_SSID_CAPACITY];
    char psk[WIFI_PSK_CAPACITY];
} wifi_credential_t;

typedef struct {
    size_t count;
    wifi_credential_t items[WIFI_MAX_NETWORKS];
} wifi_credentials_t;

typedef struct {
    char ssid[WIFI_SSID_CAPACITY];
    int rssi, authmode, channel;
} wifi_access_point_t;

typedef struct {
    size_t count;
    wifi_access_point_t items[WIFI_MAX_ACCESS_POINTS];
} wifi_scan_t;

typedef struct {
    size_t count;
    char items[WIFI_MAX_NETWORKS][WIFI_SSID_CAPACITY];
} wifi_saved_names_t;

/* Callback synchronizes its own state. NULL means no cancellation. */
typedef struct {
    bool (*requested)(void *context);
    void *context;
} wifi_cancel_t;

/* Owns data; append failure preserves previous contents. limit excludes NUL. */
typedef struct {
    char *data;
    size_t length, capacity, limit;
} wifi_buffer_t;

void wifi_buffer_init(wifi_buffer_t *buffer, size_t limit);

void wifi_buffer_free(wifi_buffer_t *buffer);

bool wifi_buffer_append(wifi_buffer_t *buffer, const void *bytes, size_t length);

bool wifi_buffer_text(wifi_buffer_t *buffer, const char *text);

bool wifi_buffer_printf(wifi_buffer_t *buffer, const char *format, ...);

char *wifi_buffer_take(wifi_buffer_t *buffer);

bool wifi_copy(char *output, size_t capacity, const char *text);

void wifi_error(char *output, const char *message);

bool wifi_cancelled(const wifi_cancel_t *cancel);

uint64_t wifi_now_ms(void);

void wifi_sleep_ms(unsigned milliseconds);

bool wifi_field(const char *text, const char *key, char *output, size_t capacity);

bool wifi_utf8_valid(const char *text, size_t length);

#ifdef __cplusplus
}
#endif

#endif
