#include "ring_buffer.hpp"
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace snap {

/**
 * Shared Memory Communication Link.
 * I built this using a file-backed mmap'd RingBuffer. It's the absolute 
 * fastest way to talk between two different processes on the same machine.
 */
template<typename T, size_t Cap = 65536>
class ShmLink final : public ILink<T> {
    RingBuffer<T, Cap>* _rb = nullptr;
    std::string _name;
    size_t _sz = 0;
    int _fd = -1;

public:
    ShmLink(const char* name) : _name(name) {
        _sz = sizeof(RingBuffer<T, Cap>);
        
#ifdef _WIN32
        std::string win_name = "Local\\" + _name;
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, static_cast<DWORD>(_sz), win_name.c_str());
        if (h) {
            _rb = static_cast<RingBuffer<T, Cap>*>(MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, _sz));
            CloseHandle(h); // Mapping remains valid
        }
#else
        std::string shm_path = "/" + _name;
        _fd = shm_open(shm_path.c_str(), O_RDWR | O_CREAT, 0666);
        if (_fd < 0) return;
        
        if (ftruncate(_fd, _sz) < 0) { close(_fd); return; }
        
        void* ptr = mmap(NULL, _sz, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
        if (ptr == MAP_FAILED) { close(_fd); return; }
        
        _rb = static_cast<RingBuffer<T, Cap>*>(ptr);
        mlock(_rb, _sz);
#endif
    }

    ~ShmLink() {
#ifdef _WIN32
        if (_rb) UnmapViewOfFile(_rb);
#else
        if (_rb) {
            munlock(_rb, _sz);
            munmap(_rb, _sz);
        }
        if (_fd >= 0) close(_fd);
#endif
    }

    // Direct access to our RingBuffer methods. Super low latency.
    SNAP_HOT bool send(const T& m) noexcept override { return _rb && _rb->push(m); }
    SNAP_HOT bool recv(T& m) noexcept override       { return _rb && _rb->pop(m);  }
    
    // Batch operations are great for throughput
    size_t send_n(const T* msgs, size_t n) noexcept { return _rb ? _rb->push_n(msgs, n) : 0; }
    size_t recv_n(T* msgs, size_t n) noexcept       { return _rb ? _rb->pop_n(msgs, n) : 0;  }

    SNAP_FORCE_INLINE size_t size() const noexcept { return _rb ? _rb->size() : 0; }
};
} // namespace snap

} // namespace snap