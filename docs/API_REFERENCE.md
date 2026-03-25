# SNAP API REFERENCE
**Version 3.0.0**

## NAMESPACE: `snap`

### CONSTANTS
- `snap::VERSION`: `constexpr string_view "3.0.0"`
- `snap::SNAP_CACHE_LINE`: `64` (bytes)

---

## `ILink<T>` (snap.hpp)
Base interface for all links.

- `virtual bool send(const T& m) noexcept = 0`
  - Send a message. Returns `false` if buffer full (non-blocking).
- `virtual bool recv(T& m) noexcept = 0`
  - Receive a message. Returns `false` if no message available (non-blocking).

---

## `snap::connect<T, Cap>(uri)` (snap.hpp)
Factory that returns `std::unique_ptr<ILink<T>>`.

**Template parameters:**
- `T`: Message type. Must be trivially copyable.
- `Cap`: Ring buffer capacity. **MUST be a power of 2**. Default = 65536.

**URI schemes:**
- `"inproc://..."`: Thread-to-thread (`InprocLink<T,Cap>`)
- `"shm://name"`: Shared memory IPC (`ShmLink<T,Cap>`)
- `"udp://IP:PORT"`: UDP sender
- `"udp://@IP:PORT"`: UDP receiver
- `"tcp://IP:PORT"`: TCP connector (client)
- `"tcp://@IP:PORT"`: TCP listener (server) — blocks until first client connects
- `"ipc://PATH"`: AF_UNIX SOCK_SEQPACKET client
- `"ipc://@PATH"`: AF_UNIX SOCK_SEQPACKET server — blocks until client connects
- `"http://IP:PORT"`: HTTP 1.1 Connector
- `"https://IP:PORT"`: Secure HTTPS 1.1
- `"ws://IP:PORT"`: WebSocket client
- `"wss://IP:PORT"`: Secure WSS client

---

## `HttpServer<Handler>` (includes/http_server.hpp)
Ultra-fast polling HTTP 1.1 reactor.

- `HttpServer(port, handler)`
- `start(pin_core)`: Starts polling worker on specified core.
- `stop()`: Halts worker and closes socket.

**Structures:**
- `HttpRequest`: Contains `method`, `path`, `headers`, `body`.
- `HttpResponse`: Contains `status`, `headers`, `body`.

---

## `WsServer<Handler>` (includes/ws_server.hpp)
Ultra-fast WebSocket reactor with AVX2 SIMD acceleration.

- `WsServer(port, handler)`
- `start(pin_core)`: Starts AVX2 worker reactor.
- `stop()`: Halts worker.

**Structures:**
- `WsFrame`: Contains `fin`, `opcode`, `masked`, `payload`, `payload_len`.

---

## `SslLink<T>` (includes/ssl_link.hpp)
Non-blocking OpenSSL wrapper for encrypted messaging.

- `send(const T& m)` / `recv(T& m)`: Standard messaging.
- `send_raw(data, len)` / `recv_raw(data, len)`: Byte-level encrypted I/O.
- `handshake()`: Poll TLS handshake progress.

---

## `SslContext` (includes/ssl_link.hpp)
OpenSSL context manager.

- `SslContext(is_server)`
- `load_cert_file(cert_path, key_path)`: Prepare PEM certificates.

---

## `RingBuffer<T, Cap>` (includes/ring_buffer.hpp)
LMAX Disruptor-style SPSC lock-free ring buffer.
`Cap` must be a power of 2.

- `bool push(const T& data) noexcept`
  - Single-producer push. Returns `false` if full.
- `bool pop(T& out) noexcept`
  - Single-consumer pop. Returns `false` if empty.
- `size_t push_n(const T* data, size_t count) noexcept`
  - Batch push. Returns number of elements actually pushed.
- `size_t pop_n(T* out, size_t count) noexcept`
  - Batch pop. Returns number of elements actually popped.

---

## `MemoryPool<T, Slots, UseHugePages=false>` (includes/memory_pool.hpp)
Lock-free slab allocator. Pre-allocates `Slots` objects at construction.
`Slots` must be a power of 2.

- `T* allocate() noexcept`
- `void deallocate(T*) noexcept`

---

## `Dispatcher<MsgType, Handler, Cap=4096>` (includes/dispatch.hpp)
Zero-allocation pub/sub dispatcher.

- `Dispatcher(Handler h = {})`
- `void subscribe(Handler h) noexcept`
- `void poll() / drain() / drain_n(max)`

---

## UTILITIES (includes/utils.hpp)
- `void cpu_relax()`: Spin-wait hint (PAUSE/ISB/YIELD)
- `void spin_wait(int n)`
- `uint64_t timestamp_ns()`
- `uint64_t rdtsc()`: CPU cycle counter
- `bool pin_thread(int core_id)`
- `bool set_thread_name(const char*)`
- `int numa_node_of_cpu(int cpu_id)`
- `bool is_power_of_two(T v)`
- `bool starts_with(string_view, prefix)`

### MACROS
- `SNAP_FORCE_INLINE`, `SNAP_HOT`, `SNAP_COLD`
- `SNAP_LIKELY(x)`, `SNAP_UNLIKELY(x)`
- `SNAP_PREFETCH_R(p)`, `SNAP_PREFETCH_W(p)`
- `SNAP_NODISCARD`, `SNAP_RESTRICT`
