#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "utils.hpp"

namespace snap {

template<typename T>
class TcpLink : public ILink<T> {
    int _fd;

    static void set_nonblock(int fd) noexcept {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    static void apply_opts(int fd) noexcept {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,  &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE, &one, sizeof(one));

#ifdef TCP_QUICKACK
        setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif

        int sndbuf = static_cast<int>(sizeof(T)) * 64;
        int rcvbuf = static_cast<int>(sizeof(T)) * 64;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        int busy_poll = 50;
        setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));

        int prio = 7;
        setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio));

        int tos = 0xB8;
        setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

#ifdef SNAP_ENABLE_ZEROCOPY
        int zc = 1;
        setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &zc, sizeof(zc));
#endif
    }

    static struct sockaddr_in parse_addr(const char* ip_port) noexcept {
        std::string s(ip_port);
        auto pos = s.find(':');
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(std::stoi(s.substr(pos + 1)));
        addr.sin_addr.s_addr = inet_addr(s.substr(0, pos).c_str());
        return addr;
    }

public:
    static TcpLink* connect(const char* ip_port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("snap: tcp socket"); exit(1); }
        apply_opts(fd);

        auto addr = parse_addr(ip_port);

#ifdef TCP_FASTOPEN_CONNECT
        int tfo = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, &tfo, sizeof(tfo));
#endif

        ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

        bool ready = false;
        for (int attempts = 0; attempts < 1000 && !ready; ++attempts) {
            struct pollfd pfd{fd, POLLOUT, 0};
            if (poll(&pfd, 1, 5) > 0 && (pfd.revents & POLLOUT)) {
                int err = 0; socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                ready = (err == 0);
            }
            if (!ready) cpu_relax();
        }
        if (!ready) { close(fd); return nullptr; }

        set_nonblock(fd);
        auto* link = new TcpLink();
        link->_fd = fd;
        return link;
    }

    static int listen_socket(const char* ip_port) {
        int srv = socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0) { perror("snap: tcp listen socket"); exit(1); }
        int one = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(srv, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
        auto addr = parse_addr(ip_port);
        if (bind(srv, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            perror("snap: tcp bind"); exit(1);
        }
        if (::listen(srv, 8) < 0) { perror("snap: tcp listen"); exit(1); }
        return srv;
    }

    static TcpLink* accept(int listen_fd) {
        struct sockaddr_in cli{};
        socklen_t cli_len = sizeof(cli);
        int fd = ::accept4(listen_fd, reinterpret_cast<struct sockaddr*>(&cli), &cli_len, SOCK_NONBLOCK);
        if (fd < 0) return nullptr;
        apply_opts(fd);
        auto* link = new TcpLink();
        link->_fd = fd;
        return link;
    }

    ~TcpLink() override { if (_fd >= 0) close(_fd); }

    SNAP_HOT SNAP_FORCE_INLINE bool send(const T& m) noexcept override {
#ifdef SNAP_ENABLE_ZEROCOPY
        return ::send(_fd, &m, sizeof(T), MSG_DONTWAIT | MSG_ZEROCOPY) == sizeof(T);
#else
        return ::send(_fd, &m, sizeof(T), MSG_DONTWAIT) == sizeof(T);
#endif
    }

    SNAP_HOT SNAP_FORCE_INLINE bool recv(T& m) noexcept override {
#ifdef TCP_QUICKACK
        int one = 1;
        setsockopt(_fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif
        return ::recv(_fd, &m, sizeof(T), MSG_DONTWAIT) == sizeof(T);
    }

private:
    TcpLink() : _fd(-1) {}
};

} // namespace snap