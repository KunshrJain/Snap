#pragma once
#include "utils.hpp"
#include <atomic>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

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
        
#ifdef _WIN32
        _mem = VirtualAlloc(NULL, _sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (_mem) VirtualLock(_mem, _sz);
#else
        _mem = mmap(NULL, _sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | (HugePages ? MAP_HUGETLB : 0), -1, 0);
        if (_mem == MAP_FAILED) { _mem = nullptr; return; }
        mlock(_mem, _sz);
#endif

        if (!_mem) return;

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
#ifdef _WIN32
            VirtualUnlock(_mem, _sz);
            VirtualFree(_mem, 0, MEM_RELEASE);
#else
            munlock(_mem, _sz);
            munmap(_mem, _sz);
#endif
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

    // I added these for compatibility with generic allocator patterns.
    T* allocate() noexcept { return alloc(); }
    void deallocate(T* p) noexcept { free(p); }

    static constexpr size_t capacity() noexcept { return Slots; }
};

} // namespace snap
