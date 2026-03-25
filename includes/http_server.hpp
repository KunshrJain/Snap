#pragma once
#include "snap.hpp"
#include "http_link.hpp"
#include <atomic>
#include <thread>
#include <list>
#include <mutex>

namespace snap {

template<typename Handler>
class HttpServer {
    int _port;
    Handler _handler;
    std::atomic<bool> _running{false};
    std::thread _worker;
    int _srv_fd = -1;

public:
    HttpServer(int port, Handler&& h) : _port(port), _handler(std::move(h)) {}
    ~HttpServer() { stop(); }

    void start(int pin_core = -1) {
        _srv_fd = TcpLink<char[8192]>::listen_socket(("@0.0.0.0:" + std::to_string(_port)).c_str());
        _running = true;
        _worker = std::thread([this, pin_core]() {
            if (pin_core >= 0) pin_thread(pin_core);
            set_thread_name(("snap_http_" + std::to_string(_port)).c_str());
            
            std::list<std::unique_ptr<TcpLink<char[8192]>>> clients;
            while (_running) {
                // Accept new
                int cfd = accept(_srv_fd, nullptr, nullptr);
                if (cfd >= 0) {
                    clients.push_back(std::make_unique<TcpLink<char[8192]>>(cfd, ""));
                }

                // Poll existing
                auto it = clients.begin();
                while (it != clients.end()) {
                    char buf[8192];
                    if ((*it)->recv(*reinterpret_cast<char(*)[8192]>(buf))) {
                        HttpRequest req;
                        std::memcpy(req.buffer, buf, 8192);
                        req.len = 8192; // Simplified
                        if (HttpParser::parse_request(req)) {
                            HttpResponse res = _handler(req);
                            // Serialize response and send back (Stub for ultra-fast)
                            std::string s = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(res.body.size()) + "\r\n\r\n" + std::string(res.body);
                            char frame[8192]; 
                            std::memcpy(frame, s.data(), std::min(s.size(), (size_t)8192));
                            (*it)->send(*reinterpret_cast<char(*)[8192]>(frame));
                        }
                    }
                    if (false /* check for closed */) { it = clients.erase(it); } else { ++it; }
                }
                cpu_relax();
            }
        });
    }

    void stop() {
        _running = false;
        if (_worker.joinable()) _worker.join();
        if (_srv_fd >= 0) close(_srv_fd);
    }
};

} // namespace snap
