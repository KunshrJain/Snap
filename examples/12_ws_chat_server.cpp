#include "snap/snap.hpp"
#include <iostream>
#include <vector>
#include <algorithm>

int main() {
    std::cout << "Snap v" << snap::VERSION << " WebSocket Chat Server starting on port 8081..." << std::endl;

    // Chat room state (simplified for example)
    static std::vector<int> client_fds; 

    auto handler = [](const snap::WsFrame& frame) {
        if (frame.opcode == snap::WsOpcode::TEXT) {
            std::string msg(static_cast<const char*>(frame.payload), frame.payload_len);
            std::cout << "[Chat] Received: " << msg << std::endl;
            
            // Multicast logic here (in real app, use the sessions list)
            if (msg == "!") std::cout << "Client wants to disconnect!" << std::endl;
        }
    };

    snap::WsServer server(8081, handler);
    server.start(1); // Pin worker to core 1

    std::cout << "WebSocket Chat live at ws://localhost:8081" << std::endl;
    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();
    server.stop();
    return 0;
}
