#pragma once
#include <memory>
#include <string_view>
#include "includes/utils.hpp"
#include "includes/ring_buffer.hpp"
#include "includes/mpmc_queue.hpp"
#include "includes/memory_pool.hpp"
#include "includes/dispatch.hpp"
#include "includes/pipeline.hpp"

namespace snap {

static constexpr std::string_view VERSION = "2.0.0";

template<typename T>
class ILink {
public:
    virtual ~ILink() = default;
    virtual bool send(const T& m) noexcept = 0;
    virtual bool recv(T& m) noexcept = 0;
};

} // namespace snap

#include "includes/shm_link.hpp"
#include "includes/udp_link.hpp"
#include "includes/tcp_link.hpp"
#include "includes/ipc_link.hpp"
#include "includes/multicast_link.hpp"

namespace snap {

template<typename T, size_t Cap = 65536>
class InprocLink final : public ILink<T> {
    RingBuffer<T, Cap> _rb;
public:
    SNAP_HOT SNAP_FORCE_INLINE bool send(const T& m) noexcept override { return _rb.push(m); }
    SNAP_HOT SNAP_FORCE_INLINE bool recv(T& m) noexcept override       { return _rb.pop(m);  }
    size_t send_n(const T* msgs, size_t n) noexcept { return _rb.push_n(msgs, n); }
    size_t recv_n(T* msgs, size_t n) noexcept       { return _rb.pop_n(msgs, n);  }
};

template<typename T, size_t Cap = 65536>
std::unique_ptr<ILink<T>> connect(std::string_view uri) {
    if (starts_with(uri, "inproc://") || uri.find("://") == std::string_view::npos) {
        return std::make_unique<InprocLink<T, Cap>>();
    }
    if (starts_with(uri, "shm://")) {
        return std::make_unique<ShmLink<T, Cap>>(std::string(uri.substr(6)).c_str());
    }
    if (starts_with(uri, "udp://")) {
        std::string_view addr = uri.substr(6);
        bool listener = (addr[0] == '@');
        return std::make_unique<UdpLink<T>>(listener ? addr.data() + 1 : addr.data(), !listener);
    }
    if (starts_with(uri, "tcp://")) {
        std::string_view addr = uri.substr(6);
        bool listener = (addr[0] == '@');
        if (listener) {
            int srv = TcpLink<T>::listen_socket(addr.data() + 1);
            return std::unique_ptr<ILink<T>>(TcpLink<T>::accept(srv));
        }
        return std::unique_ptr<ILink<T>>(TcpLink<T>::connect(addr.data()));
    }
    if (starts_with(uri, "ipc://")) {
        std::string_view path = uri.substr(6);
        bool listener = (path[0] == '@');
        if (listener) {
            int srv = IpcLink<T>::listen_socket(path.data() + 1);
            return std::unique_ptr<ILink<T>>(IpcLink<T>::accept(srv, path.data() + 1));
        }
        return std::unique_ptr<ILink<T>>(IpcLink<T>::connect(path.data()));
    }
    return std::make_unique<InprocLink<T, Cap>>();
}

template<typename T>
std::unique_ptr<ILink<T>> subscribe_multicast(const char* group_ip, int port, int ttl = 1) {
    return std::make_unique<MulticastLink<T>>(group_ip, port, false, ttl);
}

template<typename T>
std::unique_ptr<ILink<T>> publish_multicast(const char* group_ip, int port, int ttl = 1) {
    return std::make_unique<MulticastLink<T>>(group_ip, port, true, ttl);
}

} // namespace snap