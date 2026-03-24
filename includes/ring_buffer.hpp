#pragma once
#include <atomic>
#include <cstdint>
#include <new>
#include "utils.hpp"

namespace snap {

template<typename T, size_t Cap = 65536>
struct RingBuffer {
    static_assert(is_power_of_two(Cap), "RingBuffer capacity must be a power of two");
    static_assert(Cap >= 2, "RingBuffer capacity must be at least 2");

    static constexpr size_t MASK = Cap - 1;

    alignas(SNAP_CACHE_LINE) std::atomic<size_t> _write{0};
    alignas(SNAP_CACHE_LINE) size_t _rcache{0};

    alignas(SNAP_CACHE_LINE) std::atomic<size_t> _read{0};
    alignas(SNAP_CACHE_LINE) size_t _wcache{0};

    alignas(SNAP_CACHE_LINE) T _buf[Cap];

    SNAP_HOT SNAP_FORCE_INLINE bool push(const T& data) noexcept {
        const size_t w = _write.load(std::memory_order_relaxed);
        if (SNAP_UNLIKELY(w - _rcache >= Cap)) {
            _rcache = _read.load(std::memory_order_acquire);
            if (SNAP_UNLIKELY(w - _rcache >= Cap)) return false;
        }
        SNAP_PREFETCH_W(&_buf[(w + 1) & MASK]);
        _buf[w & MASK] = data;
        _write.store(w + 1, std::memory_order_release);
        return true;
    }

    SNAP_HOT SNAP_FORCE_INLINE bool pop(T& out) noexcept {
        const size_t r = _read.load(std::memory_order_relaxed);
        if (SNAP_UNLIKELY(r == _wcache)) {
            _wcache = _write.load(std::memory_order_acquire);
            if (SNAP_UNLIKELY(r == _wcache)) return false;
        }
        SNAP_PREFETCH_R(&_buf[(r + 1) & MASK]);
        out = _buf[r & MASK];
        _read.store(r + 1, std::memory_order_release);
        return true;
    }

    SNAP_HOT size_t push_n(const T* data, size_t count) noexcept {
        const size_t w = _write.load(std::memory_order_relaxed);
        _rcache = _read.load(std::memory_order_acquire);
        const size_t avail = Cap - (w - _rcache);
        const size_t n = (count < avail) ? count : avail;
        for (size_t i = 0; i < n; ++i) {
            _buf[(w + i) & MASK] = data[i];
        }
        if (n > 0) {
            _write.store(w + n, std::memory_order_release);
        }
        return n;
    }

    SNAP_HOT size_t pop_n(T* out, size_t count) noexcept {
        const size_t r = _read.load(std::memory_order_relaxed);
        _wcache = _write.load(std::memory_order_acquire);
        const size_t avail = _wcache - r;
        const size_t n = (count < avail) ? count : avail;
        for (size_t i = 0; i < n; ++i) {
            out[i] = _buf[(r + i) & MASK];
        }
        if (n > 0) {
            _read.store(r + n, std::memory_order_release);
        }
        return n;
    }

    SNAP_FORCE_INLINE bool empty() const noexcept {
        return _read.load(std::memory_order_acquire) == _write.load(std::memory_order_acquire);
    }

    SNAP_FORCE_INLINE bool full() const noexcept {
        const size_t w = _write.load(std::memory_order_acquire);
        const size_t r = _read.load(std::memory_order_acquire);
        return (w - r) >= Cap;
    }

    SNAP_FORCE_INLINE size_t size() const noexcept {
        const size_t w = _write.load(std::memory_order_acquire);
        const size_t r = _read.load(std::memory_order_acquire);
        return w - r;
    }

    static constexpr size_t capacity() noexcept { return Cap; }
};

} // namespace snap