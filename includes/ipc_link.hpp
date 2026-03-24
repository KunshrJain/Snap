#pragma once
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "utils.hpp"

namespace snap {

template<typename T>
class IpcLink : public ILink<T> {
    int _fd;
    std::string _path;
    bool _owner;

    static void set_nonblock(int fd) noexcept {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    IpcLink(int fd, std::string path, bool owner)
        : _fd(fd), _path(std::move(path)), _owner(owner) {}

public:
    static IpcLink* connect(const char* path) {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0) { perror("snap: ipc socket"); exit(1); }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        bool ready = false;
        for (int i = 0; i < 500 && !ready; ++i) {
            if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                ready = true;
            } else {
                cpu_relax();
                spin_wait(100);
            }
        }
        if (!ready) { perror("snap: ipc connect"); exit(1); }

        set_nonblock(fd);
        return new IpcLink(fd, path, false);
    }

    static int listen_socket(const char* path) {
        unlink(path);
        int srv = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (srv < 0) { perror("snap: ipc listen socket"); exit(1); }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        if (bind(srv, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("snap: ipc bind"); exit(1);
        }
        if (::listen(srv, 16) < 0) { perror("snap: ipc listen"); exit(1); }
        return srv;
    }

    static IpcLink* accept(int listen_fd, const char* path) {
        int fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK);
        if (fd < 0) return nullptr;
        return new IpcLink(fd, path, true);
    }

    ~IpcLink() override {
        if (_fd >= 0) close(_fd);
        if (_owner) unlink(_path.c_str());
    }

    SNAP_HOT SNAP_FORCE_INLINE bool send(const T& m) noexcept override {
        return ::send(_fd, &m, sizeof(T), MSG_DONTWAIT | MSG_NOSIGNAL) == sizeof(T);
    }

    SNAP_HOT SNAP_FORCE_INLINE bool recv(T& m) noexcept override {
        return ::recv(_fd, &m, sizeof(T), MSG_DONTWAIT) == sizeof(T);
    }
};

} // namespace snap
