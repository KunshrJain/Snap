#include "../snap.hpp"
#include <iostream>
#include <thread>
#include <chrono>

struct Packet {
    uint64_t seq;
    double   value;
};

int main(int argc, char** argv) {
    bool is_sender = (argc < 2 || std::string(argv[1]) == "send");

    if (is_sender) {
        auto link = snap::connect<Packet>("udp://127.0.0.1:9001");
        std::cout << "[Sender] Sending 50 UDP packets...\n";
        for (uint64_t i = 0; i < 50; ++i) {
            Packet p{i, 3.14159 * i};
            while (!link->send(p)) { snap::cpu_relax(); }
            if (i < 5) std::cout << "[Sender] Sent seq=" << i << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::cout << "[Sender] Done.\n";
    } else {
        auto link = snap::connect<Packet>("udp://@127.0.0.1:9001");
        std::cout << "[Receiver] Listening on UDP :9001...\n";
        int count = 0;
        while (count < 50) {
            Packet p;
            if (link->recv(p)) {
                ++count;
                if (count <= 5) std::cout << "[Receiver] seq=" << p.seq << " val=" << p.value << "\n";
            } else {
                snap::cpu_relax();
            }
        }
        std::cout << "[Receiver] Total: " << count << "\n";
    }
    return 0;
}
