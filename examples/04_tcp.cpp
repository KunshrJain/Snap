#include "../snap.hpp"
#include <thread>
#include <iostream>
#include <chrono>

struct Order {
    uint64_t id;
    double   price;
    int      qty;
};

int main(int argc, char** argv) {
    bool is_server = (argc < 2 || std::string(argv[1]) == "server");

    if (is_server) {
        std::cout << "[Server] Listening on TCP :9002...\n";
        int srv = snap::TcpLink<Order>::listen_socket("127.0.0.1:9002");
        auto* client = snap::TcpLink<Order>::accept(srv);
        while (!client) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            client = snap::TcpLink<Order>::accept(srv);
        }
        std::cout << "[Server] Client connected.\n";
        int count = 0;
        while (count < 10) {
            Order o;
            if (client->recv(o)) {
                ++count;
                std::cout << "[Server] Order id=" << o.id << " price=" << o.price << " qty=" << o.qty << "\n";
            } else {
                snap::cpu_relax();
            }
        }
        delete client;
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto* link = snap::TcpLink<Order>::connect("127.0.0.1:9002");
        if (!link) { std::cerr << "Could not connect.\n"; return 1; }
        std::cout << "[Client] Connected. Sending orders...\n";
        for (int i = 0; i < 10; ++i) {
            Order o{static_cast<uint64_t>(i), 100.0 + i, 10 * (i + 1)};
            while (!link->send(o)) { snap::cpu_relax(); }
            std::cout << "[Client] Sent order id=" << i << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        delete link;
    }
    return 0;
}
