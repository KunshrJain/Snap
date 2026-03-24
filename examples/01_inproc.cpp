#include "../snap.hpp"
#include <thread>
#include <iostream>
#include <atomic>

struct Quote {
    int    symbol_id;
    double price;
    int    quantity;
};

int main() {
    auto link = snap::connect<Quote>("inproc://market_feed");

    std::atomic<bool> done{false};

    std::thread consumer([&]() {
        snap::pin_thread(1);
        snap::set_thread_name("consumer");
        Quote q;
        int count = 0;
        while (count < 1000) {
            if (link->recv(q)) {
                ++count;
                if (count <= 5)
                    std::cout << "[Consumer] Symbol=" << q.symbol_id
                              << " Price=" << q.price
                              << " Qty=" << q.quantity << "\n";
            } else if (done.load(std::memory_order_acquire)) {
                break;
            } else {
                snap::cpu_relax();
            }
        }
        std::cout << "[Consumer] Total received: " << count << "\n";
    });

    snap::pin_thread(0);
    snap::set_thread_name("producer");

    for (int i = 0; i < 1000; ++i) {
        while (!link->send({101 + (i % 10), 1500.0 + i * 0.01, 100 + i})) {
            snap::cpu_relax();
        }
    }

    done.store(true, std::memory_order_release);
    consumer.join();
    std::cout << "Done.\n";
    return 0;
}
