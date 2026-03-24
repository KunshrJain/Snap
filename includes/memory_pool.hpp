#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <sys/mman.h>
#include <cstring>
#include "utils.hpp"

namespace snap {

template<typename T, size_t Slots, bool UseHugePages = false>
class MemoryPool {
    static_assert(Slots > 0 && is_power_of_two(Slots), "MemoryPool Slots must be a power of two > 0");

    struct alignas(alignof(T)) Block {
        unsigned char data[sizeof(T)];
        Block* next;
    };

    Block* _mem;
    alignas(SNAP_CACHE_LINE) std::atomic<Block*> _free_head{nullptr};
    size_t _byte_size;

    static void* alloc_raw(size_t bytes) noexcept {
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        if constexpr (UseHugePages) {
            flags |= MAP_HUGETLB;
        }
        void* ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (ptr == MAP_FAILED) {
            flags &= ~MAP_HUGETLB;
            ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
        if (ptr != MAP_FAILED) {
            mlock(ptr, bytes);
#ifdef MADV_POPULATE_WRITE
            madvise(ptr, bytes, MADV_POPULATE_WRITE);
#else
            madvise(ptr, bytes, MADV_WILLNEED);
#endif
            if constexpr (UseHugePages) {
                madvise(ptr, bytes, MADV_HUGEPAGE);
            }
        }
        return ptr;
    }

public:
    MemoryPool() noexcept {
        _byte_size = sizeof(Block) * Slots;
        void* raw = alloc_raw(_byte_size);
        _mem = reinterpret_cast<Block*>(raw);
        for (size_t i = 0; i < Slots; ++i) {
            _mem[i].next = (i + 1 < Slots) ? &_mem[i + 1] : nullptr;
        }
        _free_head.store(&_mem[0], std::memory_order_relaxed);
    }

    ~MemoryPool() noexcept {
        if (_mem) munmap(_mem, _byte_size);
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    SNAP_HOT SNAP_FORCE_INLINE T* allocate() noexcept {
        Block* head = _free_head.load(std::memory_order_acquire);
        for (;;) {
            if (SNAP_UNLIKELY(!head)) return nullptr;
            if (_free_head.compare_exchange_weak(head, head->next, std::memory_order_release, std::memory_order_acquire)) {
                return reinterpret_cast<T*>(head->data);
            }
        }
    }

    SNAP_HOT SNAP_FORCE_INLINE void deallocate(T* ptr) noexcept {
        if (SNAP_UNLIKELY(!ptr)) return;
        Block* block = reinterpret_cast<Block*>(ptr);
        Block* head = _free_head.load(std::memory_order_relaxed);
        do {
            block->next = head;
        } while (!_free_head.compare_exchange_weak(head, block, std::memory_order_release, std::memory_order_relaxed));
    }

    static constexpr size_t capacity() noexcept { return Slots; }
};

} // namespace snap
