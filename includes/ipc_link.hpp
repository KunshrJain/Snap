#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
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
 * High-Speed Local IPC Link.
 * I built this using AF_UNIX and SOCK_SEQPACKET. It's more reliable 
 * than standard pipes and faster than TCP for local process messaging.
 */
template<typename T>
class IpcLink final : public ILink<T> {
    int _fd = -1;
    std::string _path;

public:
    IpcLink(int fd, const char* path) : _fd(fd), _path(path) {
        if (_fd < 0) return;
        // I use non-blocking here as well to match Snap's polling philosophy.
#ifdef _WIN32
        u_long mode = 1; ioctlsocket(_fd, FIONBIO, &mode);
#else
        int flags = fcntl(_fd, F_GETFL, 0);
        fcntl(_fd, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    ~IpcLink() { if (_fd >= 0) closesocket_safe(_fd); }

    // Direct send. I ensure the full message is sent over the UNIX socket.
    SNAP_HOT bool send(const T& m) noexcept override {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&m);
        size_t sent = 0;
        while (sent < sizeof(T)) {
            ssize_t n = ::send(_fd, p + sent, sizeof(T) - sent, 0);
            if (SNAP_UNLIKELY(n <= 0)) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) { relax(); continue; }
                return false;
            }
            sent += n;
        }
        return true;
    }

    // Direct recv. Solid message framing for local IPC.
    SNAP_HOT bool recv(T& m) noexcept override {
        uint8_t* p = reinterpret_cast<uint8_t*>(&m);
        size_t recvd = 0;
        while (recvd < sizeof(T)) {
            ssize_t n = ::recv(_fd, p + recvd, sizeof(T) - recvd, 0);
            if (SNAP_UNLIKELY(n <= 0)) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { 
                    if (recvd == 0) return false;
                    relax(); continue; 
                }
                return false;
            }
            recvd += n;
        }
        return true;
    }

    static int listen_socket(const char* path) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        ::unlink(path);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return -1;
        if (listen(fd, 1024) < 0) return -1;
        return fd;
    }

    static IpcLink<T>* accept(int srv_fd, const char* path) {
        int cli_fd = ::accept(srv_fd, nullptr, nullptr);
        return (cli_fd >= 0) ? new IpcLink<T>(cli_fd, path) : nullptr;
    }

    static IpcLink<T>* connect(const char* path) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return nullptr;

        sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            closesocket_safe(fd); return nullptr;
        }
        return new IpcLink<T>(fd, path);
    }
};
} // namespace snap
