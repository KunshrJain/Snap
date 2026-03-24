# Snap — The World's Fastest C++20 Messaging Library

[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)

**Snap** is a zero-dependency, header-only C++20 messaging library engineered for **ultra-low latency** (sub-100ns) and high-throughput workloads. It is designed to be the fastest messaging solution for thread-to-thread, process-to-process (SHM), and network (UDP/TCP/IPC) communication.

## 🚀 Key Performance (Sub-100ns)

Measurements recorded on standard x86-64 Linux with performance tuning:

| Transport | Round-Trip Latency | P99 Jitter |
| :--- | :--- | :--- |
| **Inproc (SPSC)** | **~55 ns** | **< 80 ns** |
| **Shared Memory (SHM)** | **~68 ns** | **< 100 ns** |
| **UDP (Loopback)** | **~24 µs** | **< 30 µs** |
| **TCP (Loopback)** | **~38 µs** | **< 45 µs** |
| **Throughput** | **43.5M msgs/sec** | — |

---

## 🛠 Features

*   **LMAX Disruptor-style Ring Buffer:** Lock-free, power-of-2 bitmasking, and cache-line anti-aliasing.
*   **Mechanical Sympathy:** Extensive use of `alignas(64)` and memory-order-safe `acquire/release` to minimize cache-coherence traffic.
*   **Zero-Copy SHM:** POSIX shared memory with optional **HugePage** support (`-DSNAP_ENABLE_HUGEPAGES=ON`).
*   **Kernel Bypass Hints:** Optimized network stack using `SO_BUSY_POLL`, `IP_TOS` (DSCP EF), `SO_PRIORITY`, and `TCP_NODELAY`.
*   **Zero Allocation:** All hot-path logic (including the Dispatcher) avoids heap allocations after startup.
*   **Unified URI Factory:** Simple `snap::connect<T>("udp://127.0.0.1:9000")` interface for all transports.

---

## 📦 Getting Started

### 1. Requirements
*   **C++20** (GCC 10+, Clang 11+, or MSVC 2019+)
*   **Linux** (for SHM, HugePages, and Kernel-bypass features)
*   **libnuma-dev** (optional, but recommended for NUMA-pinning)

### 2. Integration
Snap is **header-only**. Just include `snap.hpp` and link with `-lpthread -lrt`:

```cpp
#include "snap.hpp"

struct MyMessage {
    int id;
    double price;
};

int main() {
    // 1. Create a link (Inproc, SHM, UDP, TCP, etc.)
    auto link = snap::connect<MyMessage>("inproc://my_channel");

    // 2. Send low-latency messages
    link->send({1, 99.5});

    // 3. Receive without blocking
    MyMessage m;
    if (link->recv(m)) {
        // Handle message...
    }
}
```

### 3. Build & Tests
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run Latency Benchmark
./snap_bench
```

---

## 📖 Documentation
Detailed information is available in the `docs/` directory:
*   [SNAP Architecture](./docs/SNAP.md)
*   [API Reference](./docs/API_REFERENCE.md)
*   [OS Tuning Guide](./docs/TUNING.md) (for sub-100ns latencies)
*   [Benchmarks Suite](./docs/BENCHMARKS.md)

## ⚖ License
Snap is released under the **MIT License**.
See [LICENSE](LICENSE) for details. (Coming soon)
