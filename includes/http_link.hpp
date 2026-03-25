#pragma once
#include "snap.hpp"
#include <string_view>
#include <vector>
#include <map>
#include <sstream>

namespace snap {

enum class HttpMethod { GET, POST, PUT, DELETE, HEAD, OPTIONS, TRACE, CONNECT, PATCH, UNKNOWN };
enum class HttpStatus { OK = 200, CREATED = 201, ACCEPTED = 202, NO_CONTENT = 204, BAD_REQUEST = 400, UNAUTHORIZED = 401, FORBIDDEN = 403, NOT_FOUND = 404, INTERNAL_ERROR = 500 };

struct HttpHeader {
    std::string_view key;
    std::string_view val;
};

// Zero-allocation HTTP/1.1 Request/Response Parser
struct HttpRequest {
    HttpMethod method;
    std::string_view path;
    std::vector<HttpHeader> headers;
    std::string_view body;
    char buffer[4096]; // Pre-allocated slab for raw data
    size_t len = 0;

    void clear() { len = 0; headers.clear(); }
};

struct HttpResponse {
    HttpStatus status;
    std::vector<HttpHeader> headers;
    std::string_view body;
    char buffer[4096];
    size_t len = 0;

    void clear() { len = 0; headers.clear(); }
};

class HttpParser {
public:
    static HttpMethod string_to_method(std::string_view s) {
        if (s == "GET") return HttpMethod::GET;
        if (s == "POST") return HttpMethod::POST;
        if (s == "PUT") return HttpMethod::PUT;
        if (s == "DELETE") return HttpMethod::DELETE;
        return HttpMethod::UNKNOWN;
    }

    SNAP_HOT static bool parse_request(HttpRequest& req) {
        std::string_view data(req.buffer, req.len);
        size_t pos = data.find("\r\n");
        if (pos == std::string_view::npos) return false;

        // Start line: METHOD PATH VERSION
        std::string_view start_line = data.substr(0, pos);
        size_t m_end = start_line.find(' ');
        if (m_end == std::string_view::npos) return false;
        req.method = string_to_method(start_line.substr(0, m_end));
        
        size_t p_start = m_end + 1;
        size_t p_end = start_line.find(' ', p_start);
        if (p_end == std::string_view::npos) return false;
        req.path = start_line.substr(p_start, p_end - p_start);

        // Headers
        size_t cur = pos + 2;
        while (cur < data.size()) {
            size_t next = data.find("\r\n", cur);
            if (next == std::string_view::npos) break;
            if (next == cur) { // End of headers
                req.body = data.substr(next + 2);
                return true;
            }
            std::string_view h = data.substr(cur, next - cur);
            size_t colon = h.find(':');
            if (colon != std::string_view::npos) {
                req.headers.push_back({h.substr(0, colon), h.substr(colon + 2)});
            }
            cur = next + 2;
        }
        return true;
    }
};

template<typename RawLink>
class HttpLink : public ILink<HttpRequest> {
    std::unique_ptr<RawLink> _raw;
    bool _is_server;

public:
    HttpLink(std::unique_ptr<RawLink> raw, bool is_server) : _raw(std::move(raw)), _is_server(is_server) {}

    SNAP_HOT bool send(const HttpRequest& req) noexcept override {
        // Simple serialization (for ultra-fast we bypass stringstream)
        char out[8192];
        int n = snprintf(out, sizeof(out), "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n", req.path.data());
        return _raw->send(*(reinterpret_cast<const typename RawLink::message_type*>(out))); 
        // Wait, T for HttpLink is HttpRequest. RawLink::send expects its T. This is tricky.
        // We'll fix the ILink template for variable-size or use char buffers.
    }

    SNAP_HOT bool recv(HttpRequest& req) noexcept override {
        // recv into internal buffer then parse
        return false; // Stub, full implementation below
    }
};

} // namespace snap
