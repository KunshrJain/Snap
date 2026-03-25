#pragma once
#include "snap.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

namespace snap {

/**
 * Snap SslLink - v3.0.0
 * Non-blocking, zero-allocation encrypted messaging.
 * Wraps OpenSSL in a Snap-compatible ILink interface.
 */
template<typename T>
class SslLink final : public ILink<T> {
    int _fd = -1;
    SSL* _ssl = nullptr;
    bool _is_handshaked = false;
    bool _is_server;

public:
    using message_type = T;

    SslLink(int fd, SSL_CTX* ctx, bool is_server) : _fd(fd), _is_server(is_server) {
        _ssl = SSL_new(ctx);
        SSL_set_fd(_ssl, _fd);
        if (_is_server) SSL_set_accept_state(_ssl);
        else SSL_set_connect_state(_ssl);
        
        int flags = fcntl(_fd, F_GETFL, 0);
        fcntl(_fd, F_SETFL, flags | O_NONBLOCK);
    }

    ~SslLink() {
        if (_ssl) { SSL_shutdown(_ssl); SSL_free(_ssl); }
        if (_fd >= 0) close(_fd);
    }

    SNAP_HOT bool send(const T& m) noexcept override {
        if (!_is_handshaked) if (!handshake()) return false;
        int n = SSL_write(_ssl, &m, sizeof(T));
        if (n <= 0) return (SSL_get_error(_ssl, n) == SSL_ERROR_WANT_WRITE);
        return n == sizeof(T);
    }

    SNAP_HOT bool recv(T& m) noexcept override {
        if (!_is_handshaked) if (!handshake()) return false;
        int n = SSL_read(_ssl, &m, sizeof(T));
        if (n <= 0) return (SSL_get_error(_ssl, n) == SSL_ERROR_WANT_READ);
        return n == sizeof(T);
    }

    bool handshake() {
        int n = SSL_do_handshake(_ssl);
        if (n == 1) { _is_handshaked = true; return true; }
        int err = SSL_get_error(_ssl, n);
        return (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE);
    }

    // Direct byte-level access for HTTPS/WSS
    ssize_t send_raw(const void* data, size_t len) {
        if (!_is_handshaked) if (!handshake()) return -1;
        return SSL_write(_ssl, data, len);
    }

    ssize_t recv_raw(void* data, size_t len) {
        if (!_is_handshaked) if (!handshake()) return -1;
        return SSL_read(_ssl, data, len);
    }
};

} // namespace snap
