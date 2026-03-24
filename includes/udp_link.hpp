#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/udp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "utils.hpp"

namespace snap {

static constexpr int SNAP_UDP_BATCH = 64;

template<typename T>
class UdpLink : public ILink<T> {
    static_assert(sizeof(T) <= 65507, "UdpLink: message type too large for a single UDP datagram");

    int _fd;
    struct sockaddr_in _addr;
    bool _is_sender;

    struct mmsghdr _mmsgs[SNAP_UDP_BATCH];
    struct iovec    _iovecs[SNAP_UDP_BATCH];
    T               _recv_batch[SNAP_UDP_BATCH];

    void drain_zerocopy_completions() noexcept {
#ifdef SNAP_ENABLE_ZEROCOPY
        char ctrl[128];
        struct msghdr msg{};
        msg.msg_control    = ctrl;
        msg.msg_controllen = sizeof(ctrl);
        while (recvmsg(_fd, &msg, MSG_ERRQUEUE | MSG_DONTWAIT) >= 0) {
            cpu_relax();
        }
#endif
    }

public:
    UdpLink(const char* ip_port, bool is_sender) : _is_sender(is_sender) {
        _fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (_fd < 0) { perror("snap: udp socket"); exit(1); }

        int opt = 1;
        setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

        int busy_poll = 100;
        setsockopt(_fd, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));

        int prio = 7;
        setsockopt(_fd, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio));

        int tos = 0xB8;
        setsockopt(_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

#ifdef SNAP_ENABLE_ZEROCOPY
        opt = 1;
        setsockopt(_fd, SOL_SOCKET, SO_ZEROCOPY, &opt, sizeof(opt));
#endif

        int sndbuf = static_cast<int>(sizeof(T)) * 256;
        int rcvbuf = static_cast<int>(sizeof(T)) * 1024;
        setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        std::string s(ip_port);
        auto pos = s.find(':');
        std::string ip   = s.substr(0, pos);
        int         port = std::stoi(s.substr(pos + 1));

        memset(&_addr, 0, sizeof(_addr));
        _addr.sin_family      = AF_INET;
        _addr.sin_port        = htons(port);
        _addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (!_is_sender) {
            if (bind(_fd, reinterpret_cast<struct sockaddr*>(&_addr), sizeof(_addr)) < 0) {
                perror("snap: udp bind"); exit(1);
            }
        }

        fcntl(_fd, F_SETFL, O_NONBLOCK);

        for (int i = 0; i < SNAP_UDP_BATCH; ++i) {
            _iovecs[i].iov_base       = &_recv_batch[i];
            _iovecs[i].iov_len        = sizeof(T);
            _mmsgs[i].msg_hdr.msg_iov    = &_iovecs[i];
            _mmsgs[i].msg_hdr.msg_iovlen = 1;
        }
    }

    ~UdpLink() override { drain_zerocopy_completions(); close(_fd); }

    SNAP_HOT SNAP_FORCE_INLINE bool send(const T& m) noexcept override {
#ifdef SNAP_ENABLE_ZEROCOPY
        int flags = MSG_DONTWAIT | MSG_ZEROCOPY;
#else
        int flags = MSG_DONTWAIT;
#endif
        bool ok = sendto(_fd, &m, sizeof(T), flags,
                         reinterpret_cast<const struct sockaddr*>(&_addr), sizeof(_addr)) == sizeof(T);
        if constexpr (true) { // always drain completions lazily
            drain_zerocopy_completions();
        }
        return ok;
    }

    SNAP_HOT SNAP_FORCE_INLINE bool recv(T& m) noexcept override {
        return recvfrom(_fd, &m, sizeof(T), MSG_DONTWAIT, nullptr, nullptr) == sizeof(T);
    }

    SNAP_HOT int recv_batch(T* out, int max_msgs) noexcept {
        if (max_msgs > SNAP_UDP_BATCH) max_msgs = SNAP_UDP_BATCH;
        for (int i = 0; i < max_msgs; ++i) {
            _iovecs[i].iov_base = &out[i];
        }
        int n = recvmmsg(_fd, _mmsgs, max_msgs, MSG_DONTWAIT, nullptr);
        return n > 0 ? n : 0;
    }
};

} // namespace snap