#include "snap.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define closesocket_safe(fd) closesocket(fd)
#else
#  define closesocket_safe(fd) close(fd)
#endif

namespace snap {

/**
 * Snap SslLink - v3.0.0
 * I built this as a high-speed SSL/TLS wrapper. It uses non-blocking BIOs 
 * to ensure that encryption doesn't kill our latency.
 */
struct SslCtx {
    SSL_CTX* ctx;
    bool srv;

    SslCtx(bool is_srv = false) : srv(is_srv) {
        static bool init = false;
        if (!init) {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
            init = true;
        }
        ctx = SSL_CTX_new(srv ? TLS_server_method() : TLS_client_method());
        if (!ctx) { perror("SslCtx init fail"); exit(1); }
    }

    void load(const char* cert, const char* key) {
        if (SSL_CTX_use_certificate_chain_file(ctx, cert) <= 0) exit(1);
        if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0) exit(1);
        if (!SSL_CTX_check_private_key(ctx)) exit(1);
    }

    ~SslCtx() { if (ctx) SSL_CTX_free(ctx); }
};
using SslContext = SslCtx;

template<typename T>
class SslLink final : public ILink<T> {
    int _fd = -1;
    SSL* _ssl = nullptr;
    bool _shaked = false;
    bool _srv;

public:
    using msg_t = T;

    SslLink(int fd, SSL_CTX* ctx, bool is_srv) : _fd(fd), _srv(is_srv) {
        if (_fd < 0) return;
        _ssl = SSL_new(ctx);
        SSL_set_fd(_ssl, _fd);
        if (_srv) SSL_set_accept_state(_ssl);
        else SSL_set_connect_state(_ssl);
        
#ifdef _WIN32
        u_long mode = 1; ioctlsocket(_fd, FIONBIO, &mode);
#else
        int flags = fcntl(_fd, F_GETFL, 0);
        fcntl(_fd, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    ~SslLink() {
        if (_ssl) { SSL_shutdown(_ssl); SSL_free(_ssl); }
        if (_fd >= 0) closesocket_safe(_fd);
    }

    // Encrypted send. I ensure the full message reaches the peer.
    SNAP_HOT bool send(const T& m) noexcept override {
        if (!_shaked) if (!shake()) return false;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&m);
        size_t sent = 0;
        while (sent < sizeof(T)) {
            int n = SSL_write(_ssl, p + sent, sizeof(T) - sent);
            if (SNAP_UNLIKELY(n <= 0)) {
                int err = SSL_get_error(_ssl, n);
                if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) { relax(); continue; }
                return false;
            }
            sent += n;
        }
        return true;
    }

    // Encrypted recv. I loop until we have a full T block.
    // I return false only on non-recoverable errors or if no data is available at start.
    SNAP_HOT bool recv(T& m) noexcept override {
        if (!_shaked) if (!shake()) return false;
        uint8_t* p = reinterpret_cast<uint8_t*>(&m);
        size_t recvd = 0;
        while (recvd < sizeof(T)) {
            int n = SSL_read(_ssl, p + recvd, sizeof(T) - recvd);
            if (SNAP_UNLIKELY(n <= 0)) {
                int err = SSL_get_error(_ssl, n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    if (recvd == 0) return false;
                    relax(); continue;
                }
                return false;
            }
            recvd += n;
        }
        return true;
    }

    // TLS Handshake polling. I kept this in the hot path to avoid 
    // any initialization blocking.
    bool shake() {
        int r = SSL_do_handshake(_ssl);
        if (r == 1) { _shaked = true; return true; }
        int e = SSL_get_error(_ssl, r);
        return (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE);
    }

    // Raw byte I/O for HTTPS/WSS. Super fast.
    int send_raw(const void* d, size_t l) {
        if (!_shaked) if (!shake()) return -1;
        return SSL_write(_ssl, d, l);
    }

    int recv_raw(void* d, size_t l) {
        if (!_shaked) if (!shake()) return -1;
        return SSL_read(_ssl, d, l);
    }
    
    int fd() const { return _fd; }
};

} // namespace snap
