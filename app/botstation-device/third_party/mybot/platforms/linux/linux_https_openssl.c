/* SPDX-License-Identifier: Apache-2.0 */
/* getaddrinfo, poll, and socket options are POSIX-2001/2008 APIs that glibc
 * hides under strict -std=c99 unless a feature-test macro is defined. */
#define _POSIX_C_SOURCE 200112L

#include <mybot/mybot_build_config.h>
#include <mybot/platform/mybot_https.h>

#include "linux_platform_adapters.h"

#include <api/aosl_log.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509_vfy.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    SSL_CTX *ssl_ctx;
    SSL *ssl;
    int fd;
    size_t sent;
    size_t received;
} linux_https_connection_t;

static void log_tls_failure(SSL *ssl, const char *stage, int ssl_error, int saved_errno) {
    AOSL_LOG_ERR("[HTTPS] %s failed: ssl_error=%d errno=%d (%s)", stage, ssl_error,
                 saved_errno, saved_errno ? strerror(saved_errno) : "none");
    if (ssl) {
        long verify = SSL_get_verify_result(ssl);
        if (verify != X509_V_OK) {
            AOSL_LOG_ERR("[HTTPS] certificate verification: %ld (%s); check board time and CA store",
                         verify, X509_verify_cert_error_string(verify));
        }
    }
    unsigned long error;
    while ((error = ERR_get_error()) != 0) {
        char text[256];
        ERR_error_string_n(error, text, sizeof(text));
        AOSL_LOG_ERR("[HTTPS] OpenSSL: %s", text);
    }
}

static int set_socket_timeout(int fd, int timeout_ms) {
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    if (timeout_ms <= 0) {
        AOSL_LOG_ERR("[HTTPS] request timeout budget exhausted: %d ms", timeout_ms);
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        AOSL_LOG_ERR("[HTTPS] socket timeout configuration failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int connect_socket(const char *host, uint16_t port, int timeout_ms) {
    char service[6];
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (snprintf(service, sizeof(service), "%u", (unsigned int)port) < 0) {
        AOSL_LOG_ERR("[HTTPS] cannot format TCP port");
        return -1;
    }
    AOSL_LOG_NTC("[HTTPS] DNS resolving host=%s port=%s", host, service);
    int dns_error = getaddrinfo(host, service, &hints, &addresses);
    if (dns_error != 0) {
        int saved_errno = errno;
        AOSL_LOG_ERR("[HTTPS] DNS failed: host=%s error=%d (%s)", host, dns_error,
                     gai_strerror(dns_error));
        if (dns_error == EAI_SYSTEM) {
            AOSL_LOG_ERR("[HTTPS] DNS system error: %s", strerror(saved_errno));
        }
        return -1;
    }

    int connected_fd = -1;
    for (const struct addrinfo *address = addresses; address; address = address->ai_next) {
        char ip[INET6_ADDRSTRLEN];
        int name_error = getnameinfo(address->ai_addr, address->ai_addrlen, ip, sizeof(ip),
                                     NULL, 0, NI_NUMERICHOST);
        if (name_error != 0) {
            AOSL_LOG_ERR("[HTTPS] cannot format DNS address: %s", gai_strerror(name_error));
            continue;
        }
        AOSL_LOG_NTC("[HTTPS] TCP connecting ip=%s port=%s family=%d timeout=%d ms",
                     ip, service, address->ai_family, timeout_ms);
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            AOSL_LOG_ERR("[HTTPS] socket creation failed: %s", strerror(errno));
            continue;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            AOSL_LOG_ERR("[HTTPS] nonblocking socket configuration failed: %s", strerror(errno));
            close(fd);
            continue;
        }

        int ret = connect(fd, address->ai_addr, address->ai_addrlen);
        if (ret < 0 && errno == EINPROGRESS) {
            struct pollfd poll_fd = {.fd = fd, .events = POLLOUT};
            do {
                ret = poll(&poll_fd, 1, timeout_ms);
            } while (ret < 0 && errno == EINTR);

            int socket_error = 0;
            socklen_t error_size = sizeof(socket_error);
            if (ret > 0) {
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) < 0) {
                    AOSL_LOG_ERR("[HTTPS] TCP SO_ERROR failed: %s", strerror(errno));
                    ret = -1;
                } else if (socket_error != 0 || (poll_fd.revents & POLLOUT) == 0) {
                    AOSL_LOG_ERR("[HTTPS] TCP connect failed: ip=%s error=%d (%s) revents=0x%x",
                                 ip, socket_error, socket_error ? strerror(socket_error) : "not writable",
                                 (unsigned int)poll_fd.revents);
                    ret = -1;
                } else {
                    ret = 0;
                }
            } else {
                if (ret == 0) {
                    AOSL_LOG_ERR("[HTTPS] TCP connect timed out: ip=%s timeout=%d ms", ip, timeout_ms);
                } else {
                    AOSL_LOG_ERR("[HTTPS] TCP poll failed: ip=%s %s", ip, strerror(errno));
                }
                ret = -1;
            }
        } else if (ret < 0) {
            AOSL_LOG_ERR("[HTTPS] TCP connect failed: ip=%s %s", ip, strerror(errno));
        }

        if (ret == 0 && fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            AOSL_LOG_ERR("[HTTPS] blocking socket configuration failed: %s", strerror(errno));
            ret = -1;
        }
        if (ret == 0 && set_socket_timeout(fd, timeout_ms) == 0) {
            AOSL_LOG_NTC("[HTTPS] TCP connected ip=%s port=%s", ip, service);
            connected_fd = fd;
            break;
        }
        close(fd);
    }

    freeaddrinfo(addresses);
    if (connected_fd < 0) {
        AOSL_LOG_ERR("[HTTPS] TCP connection failed for all resolved addresses");
    }
    return connected_fd;
}

static int configure_peer_name(SSL *ssl, const char *host) {
    unsigned char address[sizeof(struct in6_addr)];
    int is_ip = inet_pton(AF_INET, host, address) == 1 || inet_pton(AF_INET6, host, address) == 1;
    X509_VERIFY_PARAM *verify = SSL_get0_param(ssl);
    if (is_ip) {
        return X509_VERIFY_PARAM_set1_ip_asc(verify, host) == 1 ? 0 : -1;
    }
    if (SSL_set_tlsext_host_name(ssl, host) != 1) {
        return -1;
    }
    return SSL_set1_host(ssl, host) == 1 ? 0 : -1;
}

static int https_connect(void **out_connection, const char *host, uint16_t port, int timeout_ms) {
    AOSL_LOG_NTC("[HTTPS] connect host=%s port=%u timeout=%d ms board_time=%lld",
                 host, (unsigned int)port, timeout_ms, (long long)time(NULL));
    if (timeout_ms <= 0) {
        AOSL_LOG_ERR("[HTTPS] connect timeout budget exhausted");
        return -1;
    }
    linux_https_connection_t *connection = calloc(1, sizeof(*connection));
    if (!connection) {
        AOSL_LOG_ERR("[HTTPS] cannot allocate connection");
        return -1;
    }
    connection->fd = -1;

    ERR_clear_error();
    errno = 0;
    connection->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!connection->ssl_ctx ||
        SSL_CTX_set_min_proto_version(connection->ssl_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_default_verify_paths(connection->ssl_ctx) != 1) {
        log_tls_failure(NULL, "TLS context/CA store setup", 0, errno);
        goto fail;
    }
    AOSL_LOG_NTC("[HTTPS] TLS context ready: minimum=TLS1.2 peer verification enabled");
    SSL_CTX_set_verify(connection->ssl_ctx, SSL_VERIFY_PEER, NULL);

    connection->fd = connect_socket(host, port, timeout_ms);
    if (connection->fd < 0) {
        goto fail;
    }

    ERR_clear_error();
    errno = 0;
    connection->ssl = SSL_new(connection->ssl_ctx);
    if (!connection->ssl || configure_peer_name(connection->ssl, host) < 0 ||
        SSL_set_fd(connection->ssl, connection->fd) != 1) {
        log_tls_failure(connection->ssl, "TLS SNI/hostname/socket setup", 0, errno);
        goto fail;
    }
    AOSL_LOG_NTC("[HTTPS] TLS handshake starting host=%s", host);
    ERR_clear_error();
    errno = 0;
    int result = SSL_connect(connection->ssl);
    int saved_errno = errno;
    if (result != 1) {
        int ssl_error = SSL_get_error(connection->ssl, result);
        log_tls_failure(connection->ssl, "TLS handshake", ssl_error, saved_errno);
        goto fail;
    }
    if (SSL_get_verify_result(connection->ssl) != X509_V_OK) {
        log_tls_failure(connection->ssl, "TLS certificate verification", 0, 0);
        goto fail;
    }
    AOSL_LOG_NTC("[HTTPS] TLS connected protocol=%s cipher=%s certificate=OK",
                 SSL_get_version(connection->ssl), SSL_get_cipher_name(connection->ssl));

    *out_connection = connection;
    return 0;

fail:
    AOSL_LOG_ERR("[HTTPS] connect failed host=%s port=%u", host, (unsigned int)port);
    if (connection->ssl) {
        SSL_free(connection->ssl);
    }
    if (connection->ssl_ctx) {
        SSL_CTX_free(connection->ssl_ctx);
    }
    if (connection->fd >= 0) {
        close(connection->fd);
    }
    free(connection);
    return -1;
}

static int https_send(void *opaque, const void *data, size_t len, int timeout_ms) {
    linux_https_connection_t *connection = opaque;
    if (set_socket_timeout(connection->fd, timeout_ms) < 0) {
        return -1;
    }
    int request = len > INT_MAX ? INT_MAX : (int)len;
    AOSL_LOG_NTC("[HTTPS] send requested=%d bytes timeout=%d ms", request, timeout_ms);
    ERR_clear_error();
    errno = 0;
    int ret = SSL_write(connection->ssl, data, request);
    int saved_errno = errno;
    if (ret <= 0) {
        int error = SSL_get_error(connection->ssl, ret);
        log_tls_failure(connection->ssl, "TLS send", error, saved_errno);
        return -1;
    }
    connection->sent += (size_t)ret;
    AOSL_LOG_NTC("[HTTPS] sent=%d bytes total=%zu", ret, connection->sent);
    return ret;
}

static int https_recv(void *opaque, void *data, size_t capacity, int timeout_ms) {
    linux_https_connection_t *connection = opaque;
    if (set_socket_timeout(connection->fd, timeout_ms) < 0) {
        return -1;
    }
    int request = capacity > INT_MAX ? INT_MAX : (int)capacity;
    AOSL_LOG_NTC("[HTTPS] receive waiting capacity=%d bytes timeout=%d ms", request, timeout_ms);
    ERR_clear_error();
    errno = 0;
    int ret = SSL_read(connection->ssl, data, request);
    int saved_errno = errno;
    if (ret > 0) {
        connection->received += (size_t)ret;
        AOSL_LOG_NTC("[HTTPS] received=%d bytes total=%zu", ret, connection->received);
        return ret;
    }
    int error = SSL_get_error(connection->ssl, ret);
    if (error == SSL_ERROR_ZERO_RETURN) {
        AOSL_LOG_NTC("[HTTPS] peer closed TLS cleanly total_received=%zu", connection->received);
        return 0;
    }
    log_tls_failure(connection->ssl, "TLS receive", error, saved_errno);
    return -1;
}

static void https_close(void *opaque) {
    linux_https_connection_t *connection = opaque;
    if (!connection) {
        return;
    }
    AOSL_LOG_NTC("[HTTPS] closing sent=%zu received=%zu bytes",
                 connection->sent, connection->received);
    if (connection->ssl) {
        (void)SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
    }
    if (connection->ssl_ctx) {
        SSL_CTX_free(connection->ssl_ctx);
    }
    if (connection->fd >= 0) {
        close(connection->fd);
    }
    free(connection);
}

static const mybot_https_ops_t s_https_ops = {
    .connect = https_connect,
    .send = https_send,
    .recv = https_recv,
    .close = https_close,
};

const mybot_https_ops_t *linux_https_platform_openssl_ops(void) {
    return &s_https_ops;
}
