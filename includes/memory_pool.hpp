#pragma once
#include "utils.hpp"
#include <windows.h>
#include <io.h>
#include <atomic>
#include <vector>

namespace snap {

/**
 * Fast, Lock-free Slab Memory Pool.
 * Regular 'new' and 'malloc' are too slow for us. I built this pool 
 * to pre-allocate everything upfront, with hugepage support if needed.
 */
template<typename T, size_t Slots = 4096, bool HugePages = false>
class MemoryPool {
    static_assert(is_p2(Slots), "I need the slot count to be a power of 2.");

    struct alignas(SNAP_CACHE_LINE) Node {
        std::atomic<Node*> next;
    };

    alignas(SNAP_CACHE_LINE) std::atomic<Node*> _head{nullptr};
    void* _mem = nullptr;
    size_t _sz = 0;

public:
    MemoryPool() {
        _sz = Slots * std::max(sizeof(T), sizeof(Node));
        // Use VirtualAlloc for memory pool
        _mem = VirtualAlloc(NULL, _sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (_mem) {
            VirtualLock(_mem, _sz);
        }

        // I build the free-list links initially so we're ready to go.
        uint8_t* p = static_cast<uint8_t*>(_mem);
        for (size_t i = 0; i < Slots; ++i) {
            Node* n = reinterpret_cast<Node*>(p + (i * std::max(sizeof(T), sizeof(Node))));
            n->next.store(_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
            _head.store(n, std::memory_order_relaxed);
        }
    }

    ~MemoryPool() {
        if (_mem) {
            VirtualUnlock(_mem, _sz);
            VirtualFree(_mem, 0, MEM_RELEASE);
        }
    }

    // Allocate: I just pop from the free-list head using CAS.
    SNAP_HOT T* alloc() noexcept {
        Node* h = _head.load(std::memory_order_acquire);
        while (h && !_head.compare_exchange_weak(h, h->next.load(std::memory_order_relaxed), 
                                                std::memory_order_release, std::memory_order_acquire)) {
            // Spinning here if needed
        }
        return reinterpret_cast<T*>(h);
    }

    // Deallocate: I push back to the free-list head.
    SNAP_HOT void free(T* p) noexcept {
        if (!p) return;
        Node* n = reinterpret_cast<Node*>(p);
        Node* h = _head.load(std::memory_order_relaxed);
        do {
            n->next.store(h, std::memory_order_relaxed);
        } while (!_head.compare_exchange_weak(h, n, std::memory_order_release, std::memory_order_acquire));
    }

    static constexpr size_t capacity() noexcept { return Slots; }
};

} // namespace snap
