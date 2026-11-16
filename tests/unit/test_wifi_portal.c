/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "wifi_portal.h"
#include "check.h"

#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mutex;
    wifi_portal_status_t current;
    char last_ssid[WIFI_SSID_CAPACITY], last_password[WIFI_PSK_CAPACITY], last_action[32];
    unsigned submissions, finishes, last_index;
} handler_t;
static void status(void *context, wifi_portal_status_t *out) {
    handler_t *handler = context;
    pthread_mutex_lock(&handler->mutex);
    *out = handler->current;
    pthread_mutex_unlock(&handler->mutex);
}
static void networks(void *context, wifi_scan_t *out) {
    (void)context;
    /* At most 32 bytes per SSID, retaining the hostile quote/script fixture. */
    *out = (wifi_scan_t){
        .count = 2,
        .items = {{"WiFi \" <script>alert(1)</script>", -45, 3, 1}, {"测试网络", -60, 0, 6}}};
}
static void saved_networks(void *context, wifi_saved_names_t *out) {
    (void)context;
    *out = (wifi_saved_names_t){.count = 2, .items = {"home", "测试网络"}};
}
static bool submit(void *context, const char *ssid, const char *password, char *error) {
    (void)error;
    handler_t *handler = context;
    pthread_mutex_lock(&handler->mutex);
    if (strcmp(handler->current.state, "connecting") == 0) {
        pthread_mutex_unlock(&handler->mutex);
        return false;
    }
    ++handler->submissions;
    CHECK(wifi_copy(handler->last_ssid, sizeof(handler->last_ssid), ssid));
    CHECK(wifi_copy(handler->last_password, sizeof(handler->last_password), password));
    handler->current =
        (wifi_portal_status_t){.state = "connecting", .concurrent = true, .busy = true};
    CHECK(wifi_copy(handler->current.ssid, sizeof(handler->current.ssid), ssid));
    pthread_mutex_unlock(&handler->mutex);
    return true;
}
static bool saved_action(void *context, const char *action, unsigned index,
                         const char *expected_ssid, char *error) {
    (void)error;
    handler_t *handler = context;
    const char *saved[] = {"home", "测试网络"};
    pthread_mutex_lock(&handler->mutex);
    CHECK(wifi_copy(handler->last_action, sizeof(handler->last_action), action));
    handler->last_index = index;
    bool valid = index < 2 && strcmp(saved[index], expected_ssid) == 0;
    handler->current.busy = valid;
    pthread_mutex_unlock(&handler->mutex);
    return valid;
}
static bool finish(void *context, char *error) {
    (void)error;
    handler_t *handler = context;
    pthread_mutex_lock(&handler->mutex);
    ++handler->finishes;
    pthread_mutex_unlock(&handler->mutex);
    return true;
}
static void complete(handler_t *handler, bool success) {
    pthread_mutex_lock(&handler->mutex);
    CHECK(wifi_copy(handler->current.state, sizeof(handler->current.state),
                    success ? "connected" : "error"));
    handler->current.busy = false;
    pthread_mutex_unlock(&handler->mutex);
}
static unsigned short free_port(int type) {
    int fd = socket(AF_INET, type, 0);
    CHECK(fd >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(0x7f000001u);
    CHECK(bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(fd, (struct sockaddr *)&address, &size) == 0);
    unsigned short port = ntohs(address.sin_port);
    close(fd);
    return port;
}
static int connect_to(unsigned short port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct timeval timeout = {3, 0};
    CHECK(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    CHECK(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7f000001u);
    CHECK(connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    return fd;
}
/* Keep the last 16 responses for assertions, and free every allocation at exit. */
static char *responses[16];
static unsigned response_slot;
static const char *raw(unsigned short port, const char *request) {
    int fd = connect_to(port);
    size_t offset = 0, length = strlen(request);
    while (offset < length) {
        ssize_t sent = send(fd, request + offset, length - offset, 0);
        if (sent <= 0)
            break;
        offset += (size_t)sent;
    }
    shutdown(fd, SHUT_WR);
    wifi_buffer_t response;
    wifi_buffer_init(&response, 65536);
    char buffer[4096];
    for (;;) {
        ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        CHECK(wifi_buffer_append(&response, buffer, (size_t)got));
    }
    close(fd);
    unsigned slot = response_slot++ % 16;
    free(responses[slot]);
    responses[slot] = wifi_buffer_take(&response);
    CHECK(responses[slot]);
    return responses[slot];
}
static const char *get(unsigned short port, const char *path) {
    wifi_buffer_t request;
    wifi_buffer_init(&request, 4096);
    CHECK(wifi_buffer_printf(&request, "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n\r\n", path,
                             (unsigned)port));
    const char *result = raw(port, request.data);
    wifi_buffer_free(&request);
    return result;
}
static const char *post(unsigned short port, const char *token, const char *path, const char *body,
                        const char *origin) {
    char expected_origin[64];
    snprintf(expected_origin, sizeof(expected_origin), "http://127.0.0.1:%u", (unsigned)port);
    wifi_buffer_t request;
    wifi_buffer_init(&request, 4096);
    CHECK(wifi_buffer_printf(
        &request,
        "POST %s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nContent-Type: application/json\r\n"
        "X-CSRF-Token: %s\r\nOrigin: %s\r\nContent-Length: %zu\r\n\r\n%s",
        path, (unsigned)port, token, origin ? origin : expected_origin, strlen(body), body));
    const char *result = raw(port, request.data);
    wifi_buffer_free(&request);
    return result;
}
static bool code(const char *response, unsigned expected) {
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "HTTP/1.1 %u ", expected);
    return strncmp(response, prefix, strlen(prefix)) == 0;
}
typedef struct {
    unsigned char bytes[1024];
    size_t size;
} packet_t;
static packet_t dns(unsigned short port, const packet_t *query) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7f000001u);
    CHECK(sendto(fd, query->bytes, query->size, 0, (struct sockaddr *)&address, sizeof(address)) ==
          (ssize_t)query->size);
    struct pollfd item = {fd, POLLIN, 0};
    packet_t response = {0};
    if (poll(&item, 1, 150) > 0) {
        ssize_t got = recv(fd, response.bytes, sizeof(response.bytes), 0);
        if (got > 0)
            response.size = (size_t)got;
    }
    close(fd);
    return response;
}
int main(void) {
    signal(SIGPIPE, SIG_IGN);
    handler_t handler = {.mutex = PTHREAD_MUTEX_INITIALIZER,
                         .current = {.state = "ap", .concurrent = true},
                         .last_index = 99};
    static const wifi_portal_handler_ops_t ops = {status, networks,     saved_networks,
                                                  submit, saved_action, finish};
    wifi_portal_handler_t callbacks = {&ops, &handler};
    wifi_portal_config_t config = {"127.0.0.1", free_port(SOCK_STREAM), free_port(SOCK_DGRAM)};
    wifi_portal_t portal;
    char error[WIFI_ERROR_CAPACITY] = {0};
    CHECK(wifi_portal_create(&config, &portal, error));
    CHECK(portal.ops->start(portal.context, &callbacks, error));
    CHECK(!portal.ops->start(portal.context, &callbacks, error));

    const char *page = get(config.http_port, "/");
    CHECK(code(page, 200));
    CHECK(strstr(page, "Content-Security-Policy:"));
    CHECK(!strstr(page, "@TOKEN@"));
    CHECK(strstr(page, "textContent"));
    CHECK(!strstr(page, "innerHTML"));
    CHECK(strstr(page, "setTimeout(leave,1000)"));
    const char *start = strstr(page, "const token='");
    CHECK(start);
    char token[65];
    memcpy(token, start + strlen("const token='"), 64);
    token[64] = 0;
    CHECK(strlen(token) == 64);
    const char *scan = get(config.http_port, "/scan");
    CHECK(code(scan, 200));
    CHECK(strstr(scan, "\"support_5g\":false"));
    CHECK(strstr(scan, "WiFi \\\" <script>"));
    CHECK(strstr(scan, "测试网络"));
    CHECK(strstr(get(config.http_port, "/saved/list"), "[\"home\",\"测试网络\"]"));
    CHECK(strstr(get(config.http_port, "/status"), "\"state\":\"ap\""));
    CHECK(strstr(get(config.http_port, "/status"), "\"busy\":false"));
    const char *probes[] = {"/generate_204",    "/hotspot-detect.html",
                            "/connecttest.txt", "/ncsi.txt",
                            "/unknown",         "/submit?ssid=leak&password=secret"};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const char *response = get(config.http_port, probes[i]);
        CHECK(code(response, 302));
        char location[96];
        snprintf(location, sizeof(location), "Location: http://127.0.0.1:%u/",
                 (unsigned)config.http_port);
        CHECK(strstr(response, location));
        CHECK(!strstr(response, "secret"));
    }
    CHECK(code(get(config.http_port, "/submit"), 405));
    CHECK(code(get(config.http_port, "/exit"), 405));
    CHECK(code(post(config.http_port, "wrong", "/submit", "{}", NULL), 403));
    CHECK(code(post(config.http_port, token, "/submit", "{}", "https://example.com"), 403));
    char attacker[512];
    snprintf(attacker, sizeof(attacker),
             "POST /exit HTTP/1.1\r\nHost: attacker\r\nOrigin: http://attacker\r\nX-CSRF-Token: %s"
             "\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
             token);
    CHECK(code(raw(config.http_port, attacker), 403));
    const char *malformed[] = {
        "GET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n",
        "GET / HTTP/1.1\nHost: x\n\n",
        "GET / HTTP/1.1\r\n Host: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\n X: y\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: +0\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\n\r\n"};
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i)
        CHECK(code(raw(config.http_port, malformed[i]), 400));
    const char *tails[] = {"Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
                           "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
                           "Content-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}",
                           "Content-Length: 1025\r\n\r\n",
                           "Content-Length: 9\r\n\r\n{}",
                           "\r\n{}"};
    for (size_t i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i) {
        char request[2048];
        snprintf(request, sizeof(request), "POST /submit HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n%s",
                 (unsigned)config.http_port, tails[i]);
        CHECK(code(raw(config.http_port, request), 400));
    }
    char huge[1200];
    strcpy(huge, "GET / HTTP/1.1\r\nHost: x\r\nX: ");
    size_t huge_prefix = strlen(huge);
    memset(huge + huge_prefix, 'x', 1100);
    strcpy(huge + huge_prefix + 1100, "\r\n\r\n");
    CHECK(code(raw(config.http_port, huge), 400));
    CHECK(code(raw(config.http_port, "PUT / HTTP/1.1\r\nHost: x\r\n\r\n"), 405));

    const char *invalid_json[] = {
        "{}", "[]", "{\"ssid\":\"a\",\"ssid\":\"b\",\"password\":\"12345678\"}",
        "{\"ssid\":\"a\",\"password\":\"short\"}",
        "{\"ssid\":\"a\",\"password\":\"12345678\",\"extra\":\"x\"}",
        "{\"ssid\":\"\\ud800\",\"password\":\"12345678\"}",
        "{\"ssid\":\"\\udc00\",\"password\":\"12345678\"}",
        "{\"ssid\":\"\\u0000\",\"password\":\"12345678\"}",
        "{\"ssid\":\"\\n\",\"password\":\"12345678\"}",
        "{\"ssid\":\"\xc0\xaf\",\"password\":\"12345678\"}",
        "{\"ssid\":\"\xed\xa0\x80\",\"password\":\"12345678\"}",
        "{\"ssid\":\"\xf0\x80\x80\xaf\",\"password\":\"12345678\"}",
        "{\"ssid\":\"a\",\"password\":8}", "{\"ssid\":\"a\",\"password\":\"12345678\",}",
        "{\"ssid\":\"a\",\"password\":\"12345678\"}{}",
        "{\"ssid\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"password\":\"12345678\"}",
        ("{\"ssid\":\"a\",\"password\":"
         "\"zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz\"}"),
        /* Embedded NUL must not truncate a credential at the C callback boundary. */
        "{\"ssid\":\"a\\u0000b\",\"password\":\"12345678\"}",
        "{\"ssid\":\"a\",\"password\":\"12345678\\u0000extra\"}",
        "{\"ssid\\u0000\":\"a\",\"password\":\"12345678\"}"};
    for (size_t i = 0; i < sizeof(invalid_json) / sizeof(invalid_json[0]); ++i)
        CHECK(code(post(config.http_port, token, "/submit", invalid_json[i], NULL), 400));
    pthread_mutex_lock(&handler.mutex);
    CHECK(handler.submissions == 0);
    pthread_mutex_unlock(&handler.mutex);
    CHECK(code(post(config.http_port, token, "/submit",
                    "{\"ssid\":\"\\u6d4b\\u8bd5\\ud83d\\ude00\",\"password\":\"a\\\"b\\\\c123\"}",
                    NULL),
               202));
    pthread_mutex_lock(&handler.mutex);
    CHECK(strcmp(handler.last_ssid, "测试😀") == 0);
    CHECK(strcmp(handler.last_password, "a\"b\\c123") == 0);
    CHECK(handler.submissions == 1);
    pthread_mutex_unlock(&handler.mutex);
    CHECK(strstr(get(config.http_port, "/status"), "\"state\":\"connecting\""));
    CHECK(strstr(get(config.http_port, "/status"), "\"busy\":true"));
    CHECK(code(
        post(config.http_port, token, "/submit", "{\"ssid\":\"home\",\"password\":\"\"}", NULL),
        409));
    complete(&handler, false);
    CHECK(strstr(get(config.http_port, "/status"), "\"state\":\"error\""));
    CHECK(strstr(get(config.http_port, "/status"), "\"busy\":false"));
    CHECK(code(
        post(config.http_port, token, "/submit", "{\"ssid\":\"home\",\"password\":\"\"}", NULL),
        202));
    complete(&handler, true);
    CHECK(strstr(get(config.http_port, "/status"), "\"state\":\"connected\""));
    CHECK(!strstr(get(config.http_port, "/status"), "password"));
    CHECK(code(
        post(config.http_port, token, "/saved/delete", "{\"index\":1,\"ssid\":\"测试网络\"}", NULL),
        202));
    CHECK(strstr(get(config.http_port, "/status"), "\"busy\":true"));
    complete(&handler, true);
    CHECK(strstr(get(config.http_port, "/status"), "\"busy\":false"));
    CHECK(code(post(config.http_port, token, "/saved/set_default",
                    "{\"index\":0,\"ssid\":\"home\"}", NULL),
               202));
    pthread_mutex_lock(&handler.mutex);
    CHECK(strcmp(handler.last_action, "set_default") == 0);
    CHECK(handler.last_index == 0);
    pthread_mutex_unlock(&handler.mutex);
    const char *invalid_saved[] = {"{\"index\":\"0\",\"ssid\":\"home\"}",
                                   "{\"index\":-1,\"ssid\":\"home\"}",
                                   "{\"index\":01,\"ssid\":\"home\"}",
                                   "{\"index\":64,\"ssid\":\"home\"}",
                                   "{\"index\":999999,\"ssid\":\"home\"}",
                                   "{\"index\":0}",
                                   "{\"index\":0,\"ssid\":4}",
                                   "{\"index\":0,\"ssid\":\"\"}",
                                   "{\"index\":0,\"ssid\":\"\\u0000\"}",
                                   "{}"};
    for (size_t i = 0; i < sizeof(invalid_saved) / sizeof(invalid_saved[0]); ++i)
        CHECK(code(post(config.http_port, token, "/saved/delete", invalid_saved[i], NULL), 400));
    CHECK(code(
        post(config.http_port, token, "/saved/delete", "{\"index\":2,\"ssid\":\"gone\"}", NULL),
        409));
    CHECK(code(
        post(config.http_port, token, "/saved/delete", "{\"index\":0,\"ssid\":\"测试网络\"}", NULL),
        409));
    CHECK(code(post(config.http_port, token, "/exit", "{\"index\":0}", NULL), 400));
    CHECK(code(post(config.http_port, token, "/unknown", "{}", NULL), 404));
    CHECK(code(post(config.http_port, token, "/exit", "{}", NULL), 202));
    CHECK(code(get(config.http_port, "/status"), 200));
    pthread_mutex_lock(&handler.mutex);
    CHECK(handler.finishes == 1);
    pthread_mutex_unlock(&handler.mutex);

    packet_t query = {.bytes = {0x12, 0x34, 1,   0,   0,   1, 0,   0,   0,   0, 0, 0, 7, 'e', 'x',
                                'a',  'm',  'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1},
                      .size = 29};
    packet_t answer = dns(config.dns_port, &query);
    CHECK(answer.size == query.size + 16);
    CHECK(answer.bytes[0] == 0x12 && answer.bytes[1] == 0x34 && answer.bytes[7] == 1);
    CHECK(answer.bytes[answer.size - 4] == 127 && answer.bytes[answer.size - 1] == 1);
    packet_t aaaa = query;
    aaaa.bytes[aaaa.size - 3] = 28;
    CHECK(dns(config.dns_port, &aaaa).size == query.size);
    packet_t edns = query;
    edns.bytes[11] = 1;
    const unsigned char opt[] = {0, 0, 41, 4, 0, 0, 0, 0, 0, 0, 0};
    memcpy(edns.bytes + edns.size, opt, sizeof(opt));
    edns.size += sizeof(opt);
    CHECK(dns(config.dns_port, &edns).size == query.size + 16);
    packet_t malformed_dns[11] = {{{1, 2, 3}, 3}, {{0}, 600}};
    size_t count = 2;
    packet_t bad = query;
    --bad.size;
    malformed_dns[count++] = bad;
    bad = query;
    bad.bytes[12] = 64;
    malformed_dns[count++] = bad;
    bad = query;
    bad.bytes[12] = 0xc0;
    bad.bytes[13] = 12; /* Self pointer. */
    malformed_dns[count++] = bad;
    bad.bytes[13] = 255; /* Out of bounds. */
    malformed_dns[count++] = bad;
    bad.bytes[13] = 0; /* Header pointer. */
    malformed_dns[count++] = bad;
    bad = query;
    bad.bytes[5] = 2;
    malformed_dns[count++] = bad;
    bad = query;
    bad.bytes[2] = 0x81;
    malformed_dns[count++] = bad;
    bad = query;
    bad.bytes[bad.size++] = 0;
    malformed_dns[count++] = bad;
    bad = edns;
    bad.bytes[bad.size - 1] = 100; /* Truncated OPT data. */
    malformed_dns[count++] = bad;
    CHECK(count == sizeof(malformed_dns) / sizeof(malformed_dns[0]));
    for (size_t i = 0; i < count; ++i)
        CHECK(dns(config.dns_port, &malformed_dns[i]).size == 0);

    /* A client that never finishes its headers cannot hold shutdown hostage. */
    int slow = connect_to(config.http_port);
    CHECK(send(slow, "GET / HTTP/1.1\r\n", 16, 0) == 16);
    wifi_sleep_ms(20);
    uint64_t before = wifi_now_ms();
    portal.ops->stop(portal.context);
    CHECK(wifi_now_ms() - before < 1000);
    close(slow);
    portal.ops->stop(portal.context);
    CHECK(portal.ops->start(portal.context, &callbacks, error));
    const char *new_page = get(config.http_port, "/");
    CHECK(code(new_page, 200));
    CHECK(!strstr(new_page, token));
    portal.ops->stop(portal.context);
    wifi_portal_destroy(&portal);
    wifi_portal_config_t invalid = {"not-an-address", 8080, 8053};
    CHECK(wifi_portal_create(&invalid, &portal, error));
    CHECK(!portal.ops->start(portal.context, &callbacks, error));
    wifi_portal_destroy(&portal);
    pthread_mutex_destroy(&handler.mutex);
    for (size_t i = 0; i < sizeof(responses) / sizeof(responses[0]); ++i)
        free(responses[i]);
    puts("Wi-Fi AP portal HTTP/JSON/CSRF, asynchronous retry, captive DNS and bounded stop: PASS");
    return 0;
}
