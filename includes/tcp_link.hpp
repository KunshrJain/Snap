#pragma once
#include "snap.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")



#include <io.h>
#include <string>

namespace snap {

/**
 * Ultra-fast TCP Link.
 * I built this as a thin wrapper over Linux sockets. I enabled 
 * TCP_NODELAY and TCP_QUICKACK here to shave off every millisecond 
 * of Nagle's delay and delayed ACKs.
 */
template<typename T>
class TcpLink final : public ILink<T> {
    int _fd = -1;
    std::string _addr;
    bool _srv = false;

public:
    TcpLink(int fd, const char* addr) : _fd(fd), _addr(addr) {
        // I tune every client socket for maximum speed.
        int val = 1;
        setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
        setsockopt(_fd, IPPROTO_TCP, TCP_QUICKACK, &val, sizeof(val));
        
        
        
        u_long mode = 1; ioctlsocket(_fd, FIONBIO, &mode);
    }

    ~TcpLink() { if (_fd >= 0) closesocket(_fd); }

    // Direct send. No extra copying.
    SNAP_HOT bool send(const T& m) noexcept override {
        int n = ::send(_fd, &m, sizeof(T), 0);
        return n == sizeof(T);
    }

    // Direct recv. Just get the payload.
    SNAP_HOT bool recv(T& m) noexcept override {
        int n = ::recv(_fd, &m, sizeof(T), 0);
        return n == sizeof(T);
    }

    // I use this for the server-side listener.
    static int listen_socket(const char* ip_port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        std::string s(ip_port);
        size_t colon = s.find(':');
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(std::stoi(s.substr(colon + 1)));
        inet_pton(AF_INET, s.substr(0, colon).c_str(), &addr.sin_addr);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return -1;
        if (listen(fd, 1024) < 0) return -1;
        return fd;
    }

    static TcpLink<T>* accept(int srv_fd) {
        int cli_fd = ::accept(srv_fd, nullptr, nullptr);
        return (cli_fd >= 0) ? new TcpLink<T>(cli_fd, "unknown") : nullptr;
    }

    static TcpLink<T>* connect(const char* ip_port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return nullptr;

        std::string s(ip_port);
        size_t colon = s.find(':');
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(std::stoi(s.substr(colon + 1)));
        inet_pton(AF_INET, s.substr(0, colon).c_str(), &addr.sin_addr);

        if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            closesocket(fd); return nullptr;
        }
        return new TcpLink<T>(fd, ip_port);
    }
    
    int fd() const { return _fd; }
};

} // namespace snap