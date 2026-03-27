#pragma once
#include "ring_buffer.hpp"
#include <windows.h>

#include <io.h>
#include <string>

namespace snap {

/**
 * Shared Memory Communication Link.
 * I built this using a file-backed mmap'd RingBuffer. It's the absolute 
 * fastest way to talk between two different processes on the same machine.
 */
template<typename T, size_t Cap = 65536>
class ShmLink final : public ILink<T> {
    RingBuffer<T, Cap>* _rb = nullptr;
    HANDLE _hMapFile = NULL;
    std::string _name;
    size_t _sz = 0;
    bool _owner = false;

public:
    ShmLink(const char* name) : _name(name) {
        _sz = sizeof(RingBuffer<T, Cap>);
        std::string win_name = "Local\\" + _name; // Windows named mapping
        _hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, static_cast<DWORD>(_sz), win_name.c_str());
        if (_hMapFile == NULL) return;

        void* ptr = MapViewOfFile(_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, _sz);
        if (ptr == NULL) return;

        _rb = static_cast<RingBuffer<T, Cap>*>(ptr);
        VirtualLock(_rb, _sz);
    }

    ~ShmLink() {
        if (_rb) {
            VirtualUnlock(_rb, _sz);
            UnmapViewOfFile(_rb);
        }
        if (_hMapFile) {
            CloseHandle(_hMapFile);
            _hMapFile = NULL;
        }
    }

    // Direct access to our RingBuffer methods. Super low latency.
    SNAP_HOT bool send(const T& m) noexcept override { return _rb && _rb->push(m); }
    SNAP_HOT bool recv(T& m) noexcept override       { return _rb && _rb->pop(m);  }
    
    // Batch operations are great for throughput
    size_t send_n(const T* msgs, size_t n) noexcept { return _rb ? _rb->push_n(msgs, n) : 0; }
    size_t recv_n(T* msgs, size_t n) noexcept       { return _rb ? _rb->pop_n(msgs, n) : 0;  }

    void set_owner(bool own) { _owner = own; }
    SNAP_FORCE_INLINE size_t size() const noexcept { return _rb ? _rb->size() : 0; }
};

} // namespace snap