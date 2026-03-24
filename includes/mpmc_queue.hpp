#pragma once
#include <atomic>
#include <cstdint>
#include <new>
#include "utils.hpp"

namespace snap {

template<typename T, size_t Cap = 4096>
struct MpmcQueue {
    static_assert(is_power_of_two(Cap), "MpmcQueue capacity must be a power of two");

    static constexpr size_t MASK = Cap - 1;

    struct alignas(SNAP_CACHE_LINE) Slot {
        std::atomic<size_t> seq;
        alignas(alignof(T)) unsigned char storage[sizeof(T)];

        T& get() noexcept { return *reinterpret_cast<T*>(storage); }
        const T& get() const noexcept { return *reinterpret_cast<const T*>(storage); }
    };

    alignas(SNAP_CACHE_LINE) Slot _slots[Cap];
    alignas(SNAP_CACHE_LINE) std::atomic<size_t> _head{0};
    alignas(SNAP_CACHE_LINE) std::atomic<size_t> _tail{0};

    MpmcQueue() noexcept {
        for (size_t i = 0; i < Cap; ++i) {
            _slots[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    SNAP_HOT bool push(const T& data) noexcept {
        Slot* slot;
        size_t pos = load_relaxed(_tail);
        for (;;) {
            slot = &_slots[pos & MASK];
            const size_t seq = slot->seq.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (_tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = load_relaxed(_tail);
            }
        }
        slot->get() = data;
        slot->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    SNAP_HOT bool pop(T& out) noexcept {
        Slot* slot;
        size_t pos = load_relaxed(_head);
        for (;;) {
            slot = &_slots[pos & MASK];
            const size_t seq = slot->seq.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = load_relaxed(_head);
            }
        }
        out = slot->get();
        slot->seq.store(pos + MASK + 1, std::memory_order_release);
        return true;
    }

    SNAP_FORCE_INLINE bool empty() const noexcept {
        return load_acquire(_head) == load_acquire(_tail);
    }

    SNAP_FORCE_INLINE size_t size() const noexcept {
        const size_t h = load_acquire(_head);
        const size_t t = load_acquire(_tail);
        return t >= h ? t - h : 0;
    }

    static constexpr size_t capacity() noexcept { return Cap - 1; }
};

} // namespace snap
