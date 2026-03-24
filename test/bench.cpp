#include "../snap.hpp"
#include <vector>
#include <numeric>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstring>

struct BenchMsg {
    uint64_t id;
    uint64_t ts_ns;
};

static constexpr size_t WARMUP = 10'000;
static constexpr size_t ITERS  = 1'000'000;

struct Stats {
    uint64_t avg, p50, p90, p95, p99, p999, min, max;
};

Stats compute(std::vector<uint64_t>& lats) {
    std::sort(lats.begin(), lats.end());
    size_t n = lats.size();
    uint64_t sum = 0;
    for (auto v : lats) sum += v;
    return {
        sum / n,
        lats[n * 50 / 100],
        lats[n * 90 / 100],
        lats[n * 95 / 100],
        lats[n * 99 / 100],
        lats[n * 999 / 1000],
        lats.front(),
        lats.back()
    };
}

void bench_inproc() {
    std::cout << "\n=== Inproc (SPSC) ===\n";
    snap::RingBuffer<BenchMsg, 131072> rb;
    std::vector<uint64_t> lats;
    lats.reserve(ITERS);

    for (size_t i = 0; i < WARMUP; ++i) {
        while (!rb.push({i, 0})) { snap::cpu_relax(); }
        BenchMsg m; while (!rb.pop(m)) { snap::cpu_relax(); }
    }

    for (size_t i = 0; i < ITERS; ++i) {
        uint64_t t0 = snap::timestamp_ns();
        while (!rb.push({i, t0})) { snap::cpu_relax(); }
        BenchMsg m; while (!rb.pop(m)) { snap::cpu_relax(); }
        lats.push_back(snap::timestamp_ns() - t0);
    }

    auto s = compute(lats);
    std::cout << "avg=" << s.avg << "ns p50=" << s.p50 << "ns p95=" << s.p95
              << "ns p99=" << s.p99 << "ns p999=" << s.p999 << "ns min=" << s.min << "ns max=" << s.max << "ns\n";
}

void bench_throughput() {
    std::cout << "\n=== Throughput (100M msgs) ===\n";
    snap::RingBuffer<BenchMsg, 131072> rb;
    const size_t N = 100'000'000;
    std::atomic<size_t> recv{0};

    std::thread cons([&]() {
        snap::pin_thread(1);
        BenchMsg m;
        while (recv.load(std::memory_order_relaxed) < N) {
            if (rb.pop(m)) recv.fetch_add(1, std::memory_order_relaxed);
            else snap::cpu_relax();
        }
    });

    snap::pin_thread(0);
    auto t0 = snap::timestamp_ns();
    for (size_t i = 0; i < N; ++i) {
        while (!rb.push({i, 0})) { snap::cpu_relax(); }
    }
    cons.join();
    double elapsed_s = (snap::timestamp_ns() - t0) / 1e9;
    std::cout << "msgs/sec=" << static_cast<uint64_t>(N / elapsed_s)
              << " (" << elapsed_s << "s for " << N << " msgs)\n";
}

int main(int argc, char** argv) {
    std::cout << "Snap v" << snap::VERSION << " Benchmark\n";
    std::cout << "=========================================\n";

    snap::pin_thread(0);

    bool do_tput = (argc > 1 && std::string(argv[1]) == "tput");
    if (do_tput) {
        bench_throughput();
    } else {
        bench_inproc();
    }
    return 0;
}