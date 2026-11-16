/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "wifi_portal.h"
#include "portal_page.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HEADER_LIMIT 1024
#define BODY_LIMIT 1024
#define FIELD_LIMIT 172
#define RESPONSE_LIMIT 65536

typedef struct {
    char *key, *value;
} header_t;
typedef struct {
    char input[HEADER_LIMIT + BODY_LIMIT + 1];
    char *method, *path, *body;
    header_t headers[HEADER_LIMIT / 4];
    size_t header_count, content_length;
} request_t;
typedef struct {
    char *key, *text;
    size_t key_length, length;
    bool number;
} json_field_t;
typedef struct {
    const char *input;
    size_t length, position, used, count;
    char arena[BODY_LIMIT + 1];
    json_field_t fields[FIELD_LIMIT];
} json_t;
typedef struct {
    char address_text[INET_ADDRSTRLEN], host[INET_ADDRSTRLEN + 8], token[65];
    unsigned short http_port, dns_port;
    struct in_addr address;
    wifi_portal_handler_t handler;
    pthread_mutex_t mutex;
    bool running, http_started, dns_started;
    int http, dns, client;
    pthread_t http_thread, dns_thread;
} socket_portal_t;

static bool json_string(wifi_buffer_t *out, const char *value) {
    static const char hex[] = "0123456789abcdef";
    size_t length = strlen(value);
    bool valid = wifi_utf8_valid(value, length);
    if (!wifi_buffer_text(out, "\""))
        return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c == '"' || c == '\\') {
            char escaped[2] = {'\\', (char)c};
            if (!wifi_buffer_append(out, escaped, sizeof(escaped)))
                return false;
        } else if (c < 0x20 || (!valid && c >= 0x80)) {
            char escaped[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            if (!wifi_buffer_append(out, c >= 0x80 ? "\\ufffd" : escaped, 6))
                return false;
        } else if (!wifi_buffer_append(out, &c, 1))
            return false;
    }
    return wifi_buffer_text(out, "\"");
}

static void json_space(json_t *json) {
    while (json->position < json->length &&
           (json->input[json->position] == ' ' || json->input[json->position] == '\n' ||
            json->input[json->position] == '\r' || json->input[json->position] == '\t'))
        ++json->position;
}

static bool json_take(json_t *json, char c) {
    json_space(json);
    if (json->position == json->length || json->input[json->position] != c)
        return false;
    ++json->position;
    return true;
}

static bool json_hex4(json_t *json, uint32_t *out) {
    *out = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (json->position == json->length)
            return false;
        unsigned char c = (unsigned char)json->input[json->position++];
        int n = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (n < 0)
            return false;
        *out = (*out << 4) | (unsigned)n;
    }
    return true;
}

static bool json_byte(json_t *json, unsigned char c) {
    if (json->used >= sizeof(json->arena))
        return false;
    json->arena[json->used++] = (char)c;
    return true;
}

static bool json_point(json_t *json, uint32_t point) {
    if (point < 0x80)
        return json_byte(json, (unsigned char)point);
    if (point < 0x800)
        return json_byte(json, (unsigned char)(0xc0 | (point >> 6))) &&
               json_byte(json, (unsigned char)(0x80 | (point & 63)));
    if (point < 0x10000)
        return json_byte(json, (unsigned char)(0xe0 | (point >> 12))) &&
               json_byte(json, (unsigned char)(0x80 | ((point >> 6) & 63))) &&
               json_byte(json, (unsigned char)(0x80 | (point & 63)));
    return json_byte(json, (unsigned char)(0xf0 | (point >> 18))) &&
           json_byte(json, (unsigned char)(0x80 | ((point >> 12) & 63))) &&
           json_byte(json, (unsigned char)(0x80 | ((point >> 6) & 63))) &&
           json_byte(json, (unsigned char)(0x80 | (point & 63)));
}

static bool json_read_string(json_t *json, char **text, size_t *length) {
    if (!json_take(json, '"'))
        return false;
    size_t start = json->used;
    *text = json->arena + start;
    while (json->position < json->length) {
        unsigned char c = (unsigned char)json->input[json->position++];
        if (c == '"') {
            *length = json->used - start;
            return json_byte(json, 0);
        }
        if (c < 0x20)
            return false;
        if (c != '\\') {
            if (!json_byte(json, c))
                return false;
            continue;
        }
        if (json->position == json->length)
            return false;
        c = (unsigned char)json->input[json->position++];
        if (c == '"' || c == '\\' || c == '/') {
            if (!json_byte(json, c))
                return false;
        } else if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') {
            unsigned char decoded = c == 'b'   ? '\b'
                                    : c == 'f' ? '\f'
                                    : c == 'n' ? '\n'
                                    : c == 'r' ? '\r'
                                               : '\t';
            if (!json_byte(json, decoded))
                return false;
        } else if (c == 'u') {
            uint32_t point;
            if (!json_hex4(json, &point))
                return false;
            if (point >= 0xd800 && point <= 0xdbff) {
                if (json->position + 2 > json->length || json->input[json->position++] != '\\' ||
                    json->input[json->position++] != 'u')
                    return false;
                uint32_t low;
                if (!json_hex4(json, &low) || low < 0xdc00 || low > 0xdfff)
                    return false;
                point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
            } else if (point >= 0xdc00 && point <= 0xdfff)
                return false;
            if (!json_point(json, point))
                return false;
        } else
            return false;
    }
    return false;
}

static bool json_object(json_t *json, const char *input, size_t length) {
    memset(json, 0, sizeof(*json));
    json->input = input;
    json->length = length;
    if (!wifi_utf8_valid(input, length) || !json_take(json, '{'))
        return false;
    if (json_take(json, '}')) {
        json_space(json);
        return json->position == length;
    }
    do {
        if (json->count == FIELD_LIMIT)
            return false;
        json_field_t *field = &json->fields[json->count];
        if (!json_read_string(json, &field->key, &field->key_length) || !json_take(json, ':'))
            return false;
        json_space(json);
        if (json->position < length && input[json->position] == '"') {
            if (!json_read_string(json, &field->text, &field->length))
                return false;
        } else {
            field->number = true;
            size_t start = json->position;
            field->text = json->arena + json->used;
            while (json->position < length && input[json->position] >= '0' &&
                   input[json->position] <= '9') {
                if (!json_byte(json, (unsigned char)input[json->position++]))
                    return false;
            }
            field->length = json->position - start;
            if (!field->length || field->length > 6 ||
                (field->length > 1 && field->text[0] == '0') || !json_byte(json, 0))
                return false;
        }
        for (size_t i = 0; i < json->count; ++i)
            if (json->fields[i].key_length == field->key_length &&
                memcmp(json->fields[i].key, field->key, field->key_length) == 0)
                return false;
        ++json->count;
        if (json_take(json, '}')) {
            json_space(json);
            return json->position == length;
        }
    } while (json_take(json, ','));
    return false;
}

static const json_field_t *json_field(const json_t *json, const char *key) {
    size_t length = strlen(key);
    for (size_t i = 0; i < json->count; ++i)
        if (json->fields[i].key_length == length && memcmp(json->fields[i].key, key, length) == 0)
            return &json->fields[i];
    return NULL;
}

static bool credential_text(const char *text, size_t length) {
    if (!wifi_utf8_valid(text, length))
        return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] == 0x7f)
            return false;
    return true;
}

static bool credentials(const json_field_t *ssid, const json_field_t *password) {
    if (!ssid || !password || ssid->number || password->number || !ssid->length ||
        ssid->length > 32 || !credential_text(ssid->text, ssid->length) ||
        !credential_text(password->text, password->length))
        return false;
    if (!password->length || (password->length >= 8 && password->length <= 63))
        return true;
    if (password->length != 64)
        return false;
    for (size_t i = 0; i < 64; ++i) {
        unsigned char c = (unsigned char)password->text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static void lower(char *text) {
    for (; *text; ++text)
        if (*text >= 'A' && *text <= 'Z')
            *text += 'a' - 'A';
}

static char *trim(char *text) {
    while (*text == ' ' || *text == '\t')
        ++text;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'))
        text[--length] = 0;
    return text;
}

static const char *header(const request_t *request, const char *key) {
    for (size_t i = 0; i < request->header_count; ++i)
        if (strcmp(request->headers[i].key, key) == 0)
            return request->headers[i].value;
    return NULL;
}

static bool parse_headers(request_t *request, size_t length) {
    char *input = request->input;
    if (memchr(input, 0, length))
        return false;
    input[length] = 0;
    char *end = strstr(input, "\r\n");
    if (!end)
        return false;
    *end = 0;
    char *split = strchr(input, ' '), *second = split ? strchr(split + 1, ' ') : NULL;
    if (!split || !second ||
        (strcmp(second + 1, "HTTP/1.1") != 0 && strcmp(second + 1, "HTTP/1.0") != 0))
        return false;
    *split = *second = 0;
    request->method = input;
    request->path = split + 1;
    if (request->path[0] != '/' || strlen(request->path) > 256)
        return false;
    for (const unsigned char *p = (const unsigned char *)request->path; *p; ++p)
        if (*p <= 0x20 || *p >= 0x7f || *p == '#')
            return false;
    for (char *line = end + 2; *line;) {
        end = strstr(line, "\r\n");
        if (!end)
            return false;
        char *colon = memchr(line, ':', (size_t)(end - line));
        if (!colon || colon == line || request->header_count == HEADER_LIMIT / 4)
            return false;
        *end = *colon = 0;
        lower(line);
        for (const unsigned char *p = (const unsigned char *)line; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-'))
                return false;
        char *value = trim(colon + 1);
        for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
            if ((*p < 0x20 && *p != '\t') || *p >= 0x7f)
                return false;
        if (header(request, line))
            return false;
        request->headers[request->header_count++] = (header_t){line, value};
        line = end + 2;
    }
    if (header(request, "transfer-encoding") || !header(request, "host"))
        return false;
    const char *value = header(request, "content-length");
    if (value) {
        if (!*value || strlen(value) > 4)
            return false;
        for (; *value; ++value) {
            if (*value < '0' || *value > '9')
                return false;
            request->content_length = request->content_length * 10 + (unsigned)(*value - '0');
        }
    }
    return request->content_length <= BODY_LIMIT;
}

static bool random_token(char token[65]) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    unsigned char data[32];
    size_t count = 0;
    while (count < sizeof(data)) {
        ssize_t got = read(fd, data + count, sizeof(data) - count);
        if (got <= 0) {
            if (got < 0 && errno == EINTR)
                continue;
            close(fd);
            return false;
        }
        count += (size_t)got;
    }
    close(fd);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(data); ++i) {
        token[2 * i] = hex[data[i] >> 4];
        token[2 * i + 1] = hex[data[i] & 15];
    }
    token[64] = 0;
    return true;
}

static uint16_t word(const unsigned char *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

/* Compression pointers must refer backwards, remain in the name area, and
 * terminate within both the step bound and the expanded DNS name limit. */
static bool dns_name(const unsigned char *data, size_t size, size_t *offset) {
    size_t pos = *offset, consumed = 0, expanded = 0;
    bool jumped = false;
    for (unsigned step = 0; step < 128; ++step) {
        if (pos >= size)
            return false;
        unsigned length = data[pos++];
        if (!jumped)
            ++consumed;
        if (!length) {
            *offset += consumed;
            return expanded < 255;
        }
        if ((length & 0xc0) == 0xc0) {
            if (pos >= size)
                return false;
            size_t target = ((length & 63) << 8) | data[pos];
            if (target < 12 || target >= pos - 1)
                return false;
            if (!jumped)
                ++consumed;
            pos = target;
            jumped = true;
        } else {
            if (length > 63 || pos + length > size || expanded + length + 1 >= 255)
                return false;
            expanded += length + 1;
            pos += length;
            if (!jumped)
                consumed += length;
        }
    }
    return false;
}

static size_t dns_response(const unsigned char *data, size_t size, struct in_addr address,
                           unsigned char out[528]) {
    if (size < 12 || size > 512 || (data[2] & 0xfa) || word(data + 4) != 1 || word(data + 6) ||
        word(data + 8) || word(data + 10) > 1)
        return 0;
    size_t end = 12;
    if (!dns_name(data, size, &end) || end + 4 > size)
        return 0;
    uint16_t type = word(data + end), klass = word(data + end + 2);
    end += 4;
    size_t complete = end;
    if (word(data + 10)) {
        if (!dns_name(data, size, &complete) || complete + 10 > size || word(data + complete) != 41)
            return 0;
        size_t extra = word(data + complete + 8);
        complete += 10;
        if (extra > size - complete)
            return 0;
        complete += extra;
    }
    if (complete != size || klass != 1)
        return 0;
    memcpy(out, data, end);
    out[2] = (unsigned char)(0x84 | (data[2] & 1));
    out[3] = out[6] = out[10] = out[11] = 0;
    out[7] = type == 1 ? 1 : 0;
    if (type == 1) {
        static const unsigned char answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4};
        memcpy(out + end, answer, sizeof(answer));
        end += sizeof(answer);
        memcpy(out + end, &address.s_addr, 4);
        end += 4;
    }
    return end;
}

static bool running(socket_portal_t *portal) {
    pthread_mutex_lock(&portal->mutex);
    bool value = portal->running;
    pthread_mutex_unlock(&portal->mutex);
    return value;
}

static bool ready(socket_portal_t *portal, int fd, short events, uint64_t deadline) {
    while (running(portal) && wifi_now_ms() < deadline) {
        struct pollfd item = {fd, events, 0};
        int result = poll(&item, 1, 100);
        if (result > 0)
            return (item.revents & events) != 0;
        if (result < 0 && errno != EINTR)
            return false;
    }
    return false;
}

static bool receive_request(socket_portal_t *portal, int fd, request_t *request) {
    memset(request, 0, sizeof(*request));
    size_t size = 0, header_size = 0;
    uint64_t deadline = wifi_now_ms() + 2000;
    while (ready(portal, fd, POLLIN, deadline)) {
        char data[512];
        ssize_t count = recv(fd, data, sizeof(data), 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (count <= 0 || (size_t)count > sizeof(request->input) - 1 - size)
            return false;
        memcpy(request->input + size, data, (size_t)count);
        size += (size_t)count;
        request->input[size] = 0;
        if (!header_size) {
            char *end = strstr(request->input, "\r\n\r\n");
            if (!end) {
                if (size >= HEADER_LIMIT)
                    return false;
                continue;
            }
            header_size = (size_t)(end - request->input) + 4;
            if (header_size > HEADER_LIMIT || !parse_headers(request, header_size - 2))
                return false;
        }
        if (size > header_size + request->content_length)
            return false;
        if (size == header_size + request->content_length) {
            request->body = request->input + header_size;
            return true;
        }
    }
    return false;
}

static void respond(socket_portal_t *portal, int fd, unsigned code, const char *body,
                    const char *type, const char *extra) {
    const char *reason = code == 200   ? "OK"
                         : code == 202 ? "Accepted"
                         : code == 302 ? "Found"
                         : code == 400 ? "Bad Request"
                         : code == 403 ? "Forbidden"
                         : code == 404 ? "Not Found"
                         : code == 405 ? "Method Not Allowed"
                         : code == 409 ? "Conflict"
                                       : "Error";
    wifi_buffer_t response;
    wifi_buffer_init(&response, RESPONSE_LIMIT);
    bool ok = wifi_buffer_printf(
        &response,
        "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nCache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n%s\r\n%s",
        code, reason, type, strlen(body), extra ? extra : "", body);
    uint64_t deadline = wifi_now_ms() + 2000;
    size_t offset = 0;
    while (ok && offset < response.length && ready(portal, fd, POLLOUT, deadline)) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        ssize_t sent = send(fd, response.data + offset, response.length - offset, flags);
        if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (sent <= 0)
            break;
        offset += (size_t)sent;
    }
    wifi_buffer_free(&response);
}

static void respond_json(socket_portal_t *portal, int fd, unsigned code, const char *body) {
    respond(portal, fd, code, body, "application/json; charset=utf-8", NULL);
}

static void reject(socket_portal_t *portal, int fd, unsigned code) {
    respond_json(portal, fd, code, "{\"error\":\"request rejected\"}");
}

static bool authorized(const socket_portal_t *portal, const request_t *request) {
    const char *host = header(request, "host"), *origin = header(request, "origin"),
               *csrf = header(request, "x-csrf-token"), *type = header(request, "content-type");
    if (!host || !origin || !csrf || !type)
        return false;
    char host80[INET_ADDRSTRLEN + 8], expected_origin[INET_ADDRSTRLEN + 16],
        media[HEADER_LIMIT + 1];
    snprintf(host80, sizeof(host80), "%s:80", portal->address_text);
    snprintf(expected_origin, sizeof(expected_origin), "http://%s", host);
    if (strcmp(host, portal->host) != 0 && !(portal->http_port == 80 && strcmp(host, host80) == 0))
        return false;
    size_t length = strcspn(type, ";");
    memcpy(media, type, length);
    media[length] = 0;
    lower(media);
    return strcmp(origin, expected_origin) == 0 && strcmp(csrf, portal->token) == 0 &&
           strcmp(trim(media), "application/json") == 0;
}

static bool route_get(socket_portal_t *portal, int fd, const request_t *request) {
    if (request->content_length) {
        reject(portal, fd, 400);
        return false;
    }
    wifi_buffer_t body;
    wifi_buffer_init(&body, RESPONSE_LIMIT);
    bool ok = true;
    if (strcmp(request->path, "/") == 0) {
        for (size_t i = 0; i < sizeof(kPortalPage) / sizeof(kPortalPage[0]); ++i) {
            const char *position = kPortalPage[i], *next;
            while ((next = strstr(position, "@TOKEN@")) != NULL) {
                ok = ok && wifi_buffer_append(&body, position, (size_t)(next - position)) &&
                     wifi_buffer_text(&body, portal->token);
                position = next + 7;
            }
            ok = ok && wifi_buffer_text(&body, position);
        }
        char extra[512];
        snprintf(extra, sizeof(extra),
                 "Content-Security-Policy: default-src 'none'; script-src 'nonce-%s'; "
                 "style-src 'nonce-%s'; connect-src 'self'; base-uri 'none'; "
                 "frame-ancestors 'none'; form-action 'none'\r\n",
                 portal->token, portal->token);
        if (ok)
            respond(portal, fd, 200, body.data, "text/html; charset=utf-8", extra);
    } else if (strcmp(request->path, "/scan") == 0) {
        wifi_scan_t scan = {0};
        portal->handler.ops->networks(portal->handler.context, &scan);
        ok = wifi_buffer_text(&body, "{\"support_5g\":false,\"aps\":[");
        for (size_t i = 0; i < scan.count && i < WIFI_MAX_ACCESS_POINTS; ++i) {
            scan.items[i].ssid[WIFI_SSID_CAPACITY - 1] = 0;
            ok = ok && (!i || wifi_buffer_text(&body, ",")) &&
                 wifi_buffer_text(&body, "{\"ssid\":") && json_string(&body, scan.items[i].ssid) &&
                 wifi_buffer_printf(&body, ",\"rssi\":%d,\"authmode\":%d}", scan.items[i].rssi,
                                    scan.items[i].authmode);
        }
        ok = ok && wifi_buffer_text(&body, "]}");
        if (ok)
            respond_json(portal, fd, 200, body.data);
    } else if (strcmp(request->path, "/saved/list") == 0) {
        wifi_saved_names_t saved = {0};
        portal->handler.ops->saved_networks(portal->handler.context, &saved);
        ok = wifi_buffer_text(&body, "[");
        for (size_t i = 0; i < saved.count && i < WIFI_MAX_NETWORKS; ++i) {
            saved.items[i][WIFI_SSID_CAPACITY - 1] = 0;
            ok = ok && (!i || wifi_buffer_text(&body, ",")) && json_string(&body, saved.items[i]);
        }
        ok = ok && wifi_buffer_text(&body, "]");
        if (ok)
            respond_json(portal, fd, 200, body.data);
    } else if (strcmp(request->path, "/status") == 0) {
        wifi_portal_status_t status = {0};
        portal->handler.ops->status(portal->handler.context, &status);
        status.state[sizeof(status.state) - 1] = status.ssid[sizeof(status.ssid) - 1] = 0;
        ok = wifi_buffer_text(&body, "{\"state\":") && json_string(&body, status.state) &&
             wifi_buffer_text(&body, ",\"ssid\":") && json_string(&body, status.ssid) &&
             wifi_buffer_printf(&body, ",\"concurrent\":%s,\"busy\":%s}",
                                status.concurrent ? "true" : "false",
                                status.busy ? "true" : "false");
        if (ok)
            respond_json(portal, fd, 200, body.data);
    } else if (strcmp(request->path, "/submit") == 0 || strcmp(request->path, "/exit") == 0 ||
               strcmp(request->path, "/saved/delete") == 0 ||
               strcmp(request->path, "/saved/set_default") == 0)
        reject(portal, fd, 405);
    else {
        char extra[128];
        snprintf(extra, sizeof(extra), "Location: http://%s/\r\n", portal->host);
        respond(portal, fd, 302, "", "text/plain", extra);
    }
    if (!ok)
        reject(portal, fd, 409);
    wifi_buffer_free(&body);
    return false;
}

static bool route(socket_portal_t *portal, int fd, const request_t *request) {
    if (strcmp(request->method, "GET") == 0)
        return route_get(portal, fd, request);
    if (strcmp(request->method, "POST") != 0) {
        reject(portal, fd, 405);
        return false;
    }
    if (!authorized(portal, request)) {
        reject(portal, fd, 403);
        return false;
    }
    json_t fields;
    if (!json_object(&fields, request->body, request->content_length)) {
        reject(portal, fd, 400);
        return false;
    }
    char error[WIFI_ERROR_CAPACITY] = {0};
    if (strcmp(request->path, "/submit") == 0) {
        const json_field_t *ssid = json_field(&fields, "ssid"),
                           *password = json_field(&fields, "password");
        if (fields.count != 2 || !credentials(ssid, password)) {
            reject(portal, fd, 400);
            return false;
        }
        if (!portal->handler.ops->submit(portal->handler.context, ssid->text, password->text,
                                         error))
            reject(portal, fd, 409);
        else
            respond_json(portal, fd, 202, "{\"status\":\"pending\"}");
    } else if (strcmp(request->path, "/saved/delete") == 0 ||
               strcmp(request->path, "/saved/set_default") == 0) {
        const json_field_t *index = json_field(&fields, "index"),
                           *ssid = json_field(&fields, "ssid");
        if (fields.count != 2 || !index || !index->number || !ssid || ssid->number ||
            !ssid->length || ssid->length > 32 || !credential_text(ssid->text, ssid->length)) {
            reject(portal, fd, 400);
            return false;
        }
        unsigned number = (unsigned)strtoul(index->text, NULL, 10);
        if (number >= 64) {
            reject(portal, fd, 400);
            return false;
        }
        if (!portal->handler.ops->saved_action(portal->handler.context, request->path + 7, number,
                                               ssid->text, error))
            reject(portal, fd, 409);
        else
            respond_json(portal, fd, 202, "{\"status\":\"pending\"}");
    } else if (strcmp(request->path, "/exit") == 0) {
        if (fields.count) {
            reject(portal, fd, 400);
            return false;
        }
        respond_json(portal, fd, 202, "{\"status\":\"pending\"}");
        return true;
    } else
        reject(portal, fd, 404);
    return false;
}

static void *http_loop(void *context) {
    socket_portal_t *portal = context;
    while (running(portal)) {
        if (!ready(portal, portal->http, POLLIN, wifi_now_ms() + 200))
            continue;
        int fd = accept(portal->http, NULL, NULL);
        if (fd < 0)
            continue;
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        fcntl(fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        pthread_mutex_lock(&portal->mutex);
        portal->client = fd;
        pthread_mutex_unlock(&portal->mutex);
        request_t request;
        bool finish = false;
        if (receive_request(portal, fd, &request))
            finish = route(portal, fd, &request);
        else if (running(portal))
            reject(portal, fd, 400);
        pthread_mutex_lock(&portal->mutex);
        close(fd);
        portal->client = -1;
        pthread_mutex_unlock(&portal->mutex);
        if (finish && running(portal)) {
            char error[WIFI_ERROR_CAPACITY] = {0};
            portal->handler.ops->finish(portal->handler.context, error);
        }
    }
    return NULL;
}

static void *dns_loop(void *context) {
    socket_portal_t *portal = context;
    while (running(portal)) {
        if (!ready(portal, portal->dns, POLLIN, wifi_now_ms() + 200))
            continue;
        unsigned char data[513], response[528];
        struct sockaddr_in peer = {0};
        socklen_t length = sizeof(peer);
        ssize_t count =
            recvfrom(portal->dns, data, sizeof(data), 0, (struct sockaddr *)&peer, &length);
        if (count <= 0)
            continue;
        size_t size = dns_response(data, (size_t)count, portal->address, response);
        if (size)
            sendto(portal->dns, response, size, 0, (struct sockaddr *)&peer, length);
    }
    return NULL;
}

static int listener(socket_portal_t *portal, int type, unsigned short port) {
    int fd = socket(AF_INET, type, 0);
    if (fd < 0)
        return -1;
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr = portal->address;
    address.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void close_sockets(socket_portal_t *portal) {
    if (portal->http >= 0)
        close(portal->http);
    if (portal->dns >= 0)
        close(portal->dns);
    portal->http = portal->dns = -1;
}

static void portal_stop(void *context) {
    socket_portal_t *portal = context;
    pthread_mutex_lock(&portal->mutex);
    portal->running = false;
    if (portal->client >= 0)
        shutdown(portal->client, SHUT_RDWR);
    pthread_mutex_unlock(&portal->mutex);
    if (portal->http_started)
        pthread_join(portal->http_thread, NULL);
    if (portal->dns_started)
        pthread_join(portal->dns_thread, NULL);
    portal->http_started = portal->dns_started = false;
    close_sockets(portal);
    memset(&portal->handler, 0, sizeof(portal->handler));
}

static bool portal_start(void *context, const wifi_portal_handler_t *handler, char *error) {
    socket_portal_t *portal = context;
    if (running(portal)) {
        wifi_error(error, "Wi-Fi portal already running");
        return false;
    }
    if (!handler || !handler->ops || !handler->ops->status || !handler->ops->networks ||
        !handler->ops->saved_networks || !handler->ops->submit || !handler->ops->saved_action ||
        !handler->ops->finish || inet_pton(AF_INET, portal->address_text, &portal->address) != 1 ||
        !portal->http_port || !portal->dns_port || !random_token(portal->token)) {
        wifi_error(error, "Invalid Wi-Fi portal configuration");
        return false;
    }
    portal->http = listener(portal, SOCK_STREAM, portal->http_port);
    portal->dns = listener(portal, SOCK_DGRAM, portal->dns_port);
    if (portal->http < 0 || portal->dns < 0 || listen(portal->http, 8) != 0) {
        wifi_error(error, "Cannot bind Wi-Fi portal sockets");
        close_sockets(portal);
        return false;
    }
    snprintf(portal->host, sizeof(portal->host), "%s", portal->address_text);
    if (portal->http_port != 80)
        snprintf(portal->host, sizeof(portal->host), "%s:%u", portal->address_text,
                 (unsigned)portal->http_port);
    portal->handler = *handler;
    pthread_mutex_lock(&portal->mutex);
    portal->running = true;
    pthread_mutex_unlock(&portal->mutex);
    int result = pthread_create(&portal->http_thread, NULL, http_loop, portal);
    portal->http_started = result == 0;
    if (!result) {
        result = pthread_create(&portal->dns_thread, NULL, dns_loop, portal);
        portal->dns_started = result == 0;
    }
    if (result) {
        wifi_error(error, "Cannot start Wi-Fi portal workers");
        portal_stop(portal);
        return false;
    }
    return true;
}

static void portal_destroy(void *context) {
    socket_portal_t *portal = context;
    portal_stop(portal);
    pthread_mutex_destroy(&portal->mutex);
    free(portal);
}

bool wifi_portal_create(const wifi_portal_config_t *config, wifi_portal_t *output, char *error) {
    static const wifi_portal_ops_t ops = {portal_start, portal_stop, portal_destroy};
    if (!output)
        return false;
    memset(output, 0, sizeof(*output));
    socket_portal_t *portal = calloc(1, sizeof(*portal));
    if (!portal) {
        wifi_error(error, "Cannot allocate Wi-Fi portal");
        return false;
    }
    const char *address = config ? config->address : "192.168.4.1";
    if (!address || !wifi_copy(portal->address_text, sizeof(portal->address_text), address) ||
        pthread_mutex_init(&portal->mutex, NULL) != 0) {
        wifi_error(error, "Invalid Wi-Fi portal configuration");
        free(portal);
        return false;
    }
    portal->http_port = config ? config->http_port : 80;
    portal->dns_port = config ? config->dns_port : 53;
    portal->http = portal->dns = portal->client = -1;
    output->ops = &ops;
    output->context = portal;
    return true;
}

void wifi_portal_destroy(wifi_portal_t *portal) {
    if (portal && portal->ops) {
        if (portal->ops->destroy)
            portal->ops->destroy(portal->context);
        memset(portal, 0, sizeof(*portal));
    }
}
