#include "../snap.hpp"
#include <iostream>
#include <thread>
#include <chrono>

struct SensorData {
    uint32_t sensor_id;
    float    temperature;
    float    pressure;
};

int main(int argc, char** argv) {
    bool is_producer = (argc < 2 || std::string(argv[1]) == "pub");

    if (is_producer) {
        auto link = snap::connect<SensorData>("shm://snap_sensors");
        std::cout << "[Publisher] Sending sensor data...\n";
        for (int i = 0; i < 100; ++i) {
            SensorData d{static_cast<uint32_t>(i % 4), 25.0f + i * 0.1f, 1013.0f - i * 0.05f};
            while (!link->send(d)) { snap::cpu_relax(); }
            if (i < 5) std::cout << "[Publisher] Sent sensor " << d.sensor_id
                                 << " temp=" << d.temperature << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "[Publisher] Done.\n";
    } else {
        auto link = snap::connect<SensorData>("shm://snap_sensors");
        std::cout << "[Subscriber] Listening on SHM...\n";
        int count = 0;
        while (count < 100) {
            SensorData d;
            if (link->recv(d)) {
                ++count;
                if (count <= 5) std::cout << "[Subscriber] Got sensor " << d.sensor_id
                                          << " temp=" << d.temperature << "\n";
            } else {
                snap::cpu_relax();
            }
        }
        std::cout << "[Subscriber] Total: " << count << "\n";
    }
    return 0;
}
