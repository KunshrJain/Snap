#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define MSG_NOSIGNAL 0
#  define closesocket_safe(fd) closesocket(fd)
#else
#  define closesocket_safe(fd) close(fd)
#endif

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
        if (_fd < 0) return;
        // I tune every client socket for maximum speed.
        int val = 1;
        setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
#ifdef TCP_QUICKACK
        setsockopt(_fd, IPPROTO_TCP, TCP_QUICKACK, &val, sizeof(val));
#endif
        
#ifdef SO_BUSY_POLL
        int poll = 50; 
        setsockopt(_fd, SOL_SOCKET, SO_BUSY_POLL, &poll, sizeof(poll));
#endif

#ifdef _WIN32
        u_long mode = 1; ioctlsocket(_fd, FIONBIO, &mode);
#else
        int flags = fcntl(_fd, F_GETFL, 0);
        fcntl(_fd, F_SETFL, flags | O_NONBLOCK);
#endif
    }

    ~TcpLink() { if (_fd >= 0) closesocket_safe(_fd); }

    // Direct send. I ensure we send the full sizeof(T) even if the kernel buffers are full.
    SNAP_HOT bool send(const T& m) noexcept override {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&m);
        size_t sent = 0;
        while (sent < sizeof(T)) {
            ssize_t n = ::send(_fd, p + sent, sizeof(T) - sent, MSG_NOSIGNAL);
            if (SNAP_UNLIKELY(n <= 0)) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) { relax(); continue; }
                return false;
            }
            sent += n;
        }
        return true;
    }

    // Direct recv. I spin until the full message is here. 
    // This is mandatory for our message-framed transport.
    SNAP_HOT bool recv(T& m) noexcept override {
        uint8_t* p = reinterpret_cast<uint8_t*>(&m);
        size_t recvd = 0;
        while (recvd < sizeof(T)) {
            ssize_t n = ::recv(_fd, p + recvd, sizeof(T) - recvd, 0);
            if (SNAP_UNLIKELY(n <= 0)) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { 
                    if (recvd == 0) return false; // Nothing yet
                    relax(); continue; 
                }
                return false;
            }
            recvd += n;
        }
        return true;
    }

    // I use this for the server-side listener.
    static int listen_socket(const char* ip_port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

        std::string s(ip_port);
        size_t colon = s.find(':');
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(std::stoi(s.substr(colon + 1)));
        inet_pton(AF_INET, s.substr(0, colon).c_str(), &addr.sin_addr);

        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            closesocket_safe(fd); return -1; 
        }
        if (listen(fd, 1024) < 0) {
            closesocket_safe(fd); return -1;
        }
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
            closesocket_safe(fd); return nullptr;
        }
        return new TcpLink<T>(fd, ip_port);
    }
    
    int fd() const { return _fd; }
};

} // namespace snap