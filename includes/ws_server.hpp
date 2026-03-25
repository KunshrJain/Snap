#pragma once
#include "snap.hpp"
#include "ws_link.hpp"
#include "http_link.hpp"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <atomic>
#include <thread>
#include <vector>
#include <list>

namespace snap {

class WsUtils {
public:
    static std::string base64_encode(const unsigned char* input, int length) {
        BIO *b64, *bio;
        BUF_MEM *bufferPtr;
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, input, length);
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &bufferPtr);
        std::string result(bufferPtr->data, bufferPtr->length);
        BIO_free_all(bio);
        return result;
    }

    static std::string handshake_response(std::string_view key) {
        static constexpr std::string_view GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string combined = std::string(key) + std::string(GUID);
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), hash);
        std::string encoded = base64_encode(hash, SHA_DIGEST_LENGTH);
        return "HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: " + encoded + "\r\n\r\n";
    }
};

template<typename Handler>
class WsServer {
    int _port;
    Handler _handler;
    std::atomic<bool> _running{false};
    std::thread _worker;

public:
    WsServer(int port, Handler&& h) : _port(port), _handler(std::move(h)) {}
    ~WsServer() { stop(); }

    void start(int pin_core = -1) {
        int srv = TcpLink<char[8192]>::listen_socket(("@0.0.0.0:" + std::to_string(_port)).c_str());
        _running = true;
        _worker = std::thread([this, srv, pin_core]() {
            if (pin_core >= 0) pin_thread(pin_core);
            std::list<std::unique_ptr<TcpLink<char[8192]>>> raw_clients;
            while (_running) {
                int cfd = accept(srv, nullptr, nullptr);
                if (cfd >= 0) raw_clients.push_back(std::make_unique<TcpLink<char[8192]>>(cfd, ""));

                auto it = raw_clients.begin();
                while (it != raw_clients.end()) {
                    char buf[8192];
                    if ((*it)->recv(*reinterpret_cast<char(*)[8192]>(buf))) {
                        // Very simplified upgrade handshake and frame poll 
                        // For ultra-speed we bypass full HTTP parser in hot path
                        std::string_view data(buf, 8192);
                        if (data.find("Upgrade: websocket") != std::string_view::npos) {
                            size_t key_pos = data.find("Sec-WebSocket-Key: ");
                            if (key_pos != std::string_view::npos) {
                                std::string_view key = data.substr(key_pos + 19, 24);
                                std::string res = WsUtils::handshake_response(key);
                                char res_buf[8192] = {0}; std::memcpy(res_buf, res.data(), res.size());
                                (*it)->send(*reinterpret_cast<char(*)[8192]>(res_buf));
                            }
                        } else {
                            WsFrame frame;
                            if (WebSocket::decode_frame(buf, 8192, frame)) {
                                _handler(frame);
                            }
                        }
                    }
                    ++it;
                }
                cpu_relax();
            }
            close(srv);
        });
    }

    void stop() { _running = false; if(_worker.joinable()) _worker.join(); }
};

} // namespace snap
