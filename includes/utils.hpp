#pragma once
#include <cstdint>
#include <ctime>
#include <sched.h>
#include <pthread.h>
#include <string_view>
#if __has_include(<numa.h>)
#  include <numa.h>
#  define SNAP_HAS_NUMA 1
#endif

namespace snap {

#define SNAP_CACHE_LINE 64
#define SNAP_FORCE_INLINE __attribute__((always_inline)) inline
#define SNAP_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SNAP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SNAP_PREFETCH_R(addr) __builtin_prefetch((addr), 0, 3)
#define SNAP_PREFETCH_W(addr) __builtin_prefetch((addr), 1, 3)
#define SNAP_HOT   __attribute__((hot))
#define SNAP_COLD  __attribute__((cold))
#define SNAP_NODISCARD [[nodiscard]]
#define SNAP_NOINLINE  __attribute__((noinline))
#define SNAP_RESTRICT  __restrict__

static_assert(__cplusplus >= 202002L, "Snap requires C++20 or later");

SNAP_FORCE_INLINE void cpu_relax() noexcept {
#if defined(__i386__) || defined(__x86_64__)
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("isb" ::: "memory");
#elif defined(__arm__)
    asm volatile("yield" ::: "memory");
#elif defined(__riscv)
    asm volatile(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}

SNAP_FORCE_INLINE void spin_wait(int n) noexcept {
    for (int i = 0; i < n; ++i) cpu_relax();
}

SNAP_FORCE_INLINE uint64_t timestamp_ns() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

SNAP_FORCE_INLINE uint64_t rdtsc() noexcept {
#if defined(__x86_64__)
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#else
    return timestamp_ns();
#endif
}

constexpr bool starts_with(std::string_view str, std::string_view prefix) noexcept {
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

inline bool pin_thread(int core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
}

inline bool set_thread_name(const char* name) noexcept {
    return pthread_setname_np(pthread_self(), name) == 0;
}

inline int numa_node_of_cpu(int cpu_id) noexcept {
#ifdef SNAP_HAS_NUMA
    return numa_available() >= 0 ? ::numa_node_of_cpu(cpu_id) : 0;
#else
    (void)cpu_id;
    return 0;
#endif
}

template<typename T>
constexpr bool is_power_of_two(T v) noexcept {
    return v > 0 && (v & (v - 1)) == 0;
}

template<size_t N>
constexpr size_t next_power_of_two() noexcept {
    size_t v = 1;
    while (v < N) v <<= 1;
    return v;
}

template<typename T>
SNAP_FORCE_INLINE T load_relaxed(const std::atomic<T>& a) noexcept {
    return a.load(std::memory_order_relaxed);
}

template<typename T>
SNAP_FORCE_INLINE T load_acquire(const std::atomic<T>& a) noexcept {
    return a.load(std::memory_order_acquire);
}

template<typename T>
SNAP_FORCE_INLINE void store_release(std::atomic<T>& a, T v) noexcept {
    a.store(v, std::memory_order_release);
}

} // namespace snap