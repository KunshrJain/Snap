#include "snap/snap.hpp"
#include <iostream>

int main() {
    std::cout << "Snap v" << snap::VERSION << " HTTP Cache Server starting on port 8080..." << std::endl;

    // Zero-allocation handler
    auto handler = [](const snap::HttpRequest& req) -> snap::HttpResponse {
        std::cout << "Request: " << req.path << " (" << (int)req.method << ")" << std::endl;
        
        snap::HttpResponse res;
        res.status = snap::HttpStatus::OK;
        
        if (req.path == "/api/hello") {
            res.body = "{\"message\": \"Hello from Ultra-Fast Snap HTTP!\"}";
        } else {
            res.body = "Snap HTTP 1.1 Server - Ultra Low Jitter";
        }
        return res;
    };

    snap::HttpServer server(8080, handler);
    server.start(0); // Pin to core 0

    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    server.stop();
    return 0;
}
