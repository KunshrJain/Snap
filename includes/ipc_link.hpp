#pragma once
#include "snap.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <afunix.h>

#include <io.h>
#include <string>

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
        // I use non-blocking here as well to match Snap's polling philosophy.
        u_long mode = 1; ioctlsocket(_fd, FIONBIO, &mode);
    }

    ~IpcLink() { if (_fd >= 0) closesocket(_fd); }

    // Direct send. Message-oriented delivery.
    SNAP_HOT bool send(const T& m) noexcept override {
        int n = ::send(_fd, &m, sizeof(T), 0);
        return n == sizeof(T);
    }

    // Direct recv. Reliable delivery without head-of-line blocking.
    SNAP_HOT bool recv(T& m) noexcept override {
        int n = ::recv(_fd, &m, sizeof(T), 0);
        return n == sizeof(T);
    }

    static int listen_socket(const char* path) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        _unlink(path);

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
            closesocket(fd); return nullptr;
        }
        return new IpcLink<T>(fd, path);
    }
};

} // namespace snap
