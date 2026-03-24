#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include "ring_buffer.hpp"

namespace snap {

template<typename T, size_t Cap = 65536, bool UseHugePages = false>
class ShmLink : public ILink<T> {
    using Buf = RingBuffer<T, Cap>;

    std::string _name;
    Buf* _rb;
    int _fd;
    bool _owner;

public:
    ShmLink(const char* name, bool owner = true) : _owner(owner) {
        _name = (name[0] == '/') ? name : std::string("/") + name;

        _fd = shm_open(_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (_fd < 0) { perror("snap: shm_open"); exit(1); }

        if (ftruncate(_fd, sizeof(Buf)) < 0) { perror("snap: ftruncate"); exit(1); }

        int flags = MAP_SHARED;
#ifdef SNAP_ENABLE_HUGEPAGES
        flags |= MAP_HUGETLB;
#endif

        void* ptr = mmap(nullptr, sizeof(Buf), PROT_READ | PROT_WRITE, flags, _fd, 0);
        if (ptr == MAP_FAILED) {
            flags &= ~MAP_HUGETLB;
            ptr = mmap(nullptr, sizeof(Buf), PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
            if (ptr == MAP_FAILED) { perror("snap: mmap"); exit(1); }
        }

        mlock(ptr, sizeof(Buf));
        madvise(ptr, sizeof(Buf), MADV_HUGEPAGE);
#ifdef MADV_POPULATE_WRITE
        madvise(ptr, sizeof(Buf), MADV_POPULATE_WRITE);
#endif

        _rb = static_cast<Buf*>(ptr);
    }

    ~ShmLink() override {
        munmap(_rb, sizeof(Buf));
        close(_fd);
        if (_owner) shm_unlink(_name.c_str());
    }

    SNAP_HOT SNAP_FORCE_INLINE bool send(const T& m) noexcept override { return _rb->push(m); }
    SNAP_HOT SNAP_FORCE_INLINE bool recv(T& m) noexcept override       { return _rb->pop(m);  }

    size_t send_n(const T* msgs, size_t count) noexcept { return _rb->push_n(msgs, count); }
    size_t recv_n(T* msgs, size_t count) noexcept       { return _rb->pop_n(msgs, count);  }
};

} // namespace snap