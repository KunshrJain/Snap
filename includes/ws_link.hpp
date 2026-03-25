#pragma once
#include "snap.hpp"
#include <string_view>
#include <vector>
#include <immintrin.h> // SIMD for masking

namespace snap {

enum class WsOpcode : uint8_t {
    CONT = 0x0, TEXT = 0x1, BINARY = 0x2, CLOSE = 0x8, PING = 0x9, PONG = 0xA
};

struct WsFrame {
    bool fin;
    WsOpcode opcode;
    bool masked;
    uint32_t mask_key;
    const void* payload;
    size_t payload_len;
};

class WebSocket {
public:
    // Fast masking using XOR and SIMD if available
    SNAP_HOT static void mask_payload(void* data, size_t len, uint32_t key) noexcept {
        uint8_t* p = static_cast<uint8_t*>(data);
        uint8_t k[4] = { static_cast<uint8_t>(key >> 24), static_cast<uint8_t>(key >> 16), 
                         static_cast<uint8_t>(key >> 8),  static_cast<uint8_t>(key) };
#if defined(__AVX2__)
        __m256i m = _mm256_set_epi8(k[3],k[2],k[1],k[0],k[3],k[2],k[1],k[0],k[3],k[2],k[1],k[0],k[3],k[2],k[1],k[0],
                                    k[3],k[2],k[1],k[0],k[3],k[2],k[1],k[0],k[3],k[2],k[1],k[0],k[3],k[2],k[1],k[0]);
        size_t i = 0;
        for (; i + 32 <= len; i += 32) {
            __m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + i), _mm256_xor_si256(d, m));
        }
        for (; i < len; ++i) p[i] ^= k[i % 4];
#else
        for (size_t i = 0; i < len; ++i) p[i] ^= k[i % 4];
#endif
    }

    SNAP_HOT static size_t encode_frame(void* buffer, size_t capacity, const WsFrame& frame) noexcept {
        uint8_t* p = static_cast<uint8_t*>(buffer);
        if (capacity < 14 + frame.payload_len) return 0; // Buffer too small

        p[0] = (frame.fin ? 0x80 : 0x00) | static_cast<uint8_t>(frame.opcode);
        size_t header_len = 2;

        if (frame.payload_len < 126) {
            p[1] = static_cast<uint8_t>(frame.payload_len);
        } else if (frame.payload_len < 65536) {
            p[1] = 126;
            p[2] = (frame.payload_len >> 8) & 0xFF;
            p[3] = frame.payload_len & 0xFF;
            header_len = 4;
        } else {
            p[1] = 127;
            for (int i = 0; i < 8; ++i) p[2 + i] = (frame.payload_len >> ((7 - i) * 8)) & 0xFF;
            header_len = 10;
        }

        if (frame.masked) {
            p[1] |= 0x80;
            p[header_len] = (frame.mask_key >> 24) & 0xFF;
            p[header_len + 1] = (frame.mask_key >> 16) & 0xFF;
            p[header_len + 2] = (frame.mask_key >> 8) & 0xFF;
            p[header_len + 3] = frame.mask_key & 0xFF;
            header_len += 4;
        }

        std::memcpy(p + header_len, frame.payload, frame.payload_len);
        if (frame.masked) mask_payload(p + header_len, frame.payload_len, frame.mask_key);
        
        return header_len + frame.payload_len;
    }

    SNAP_HOT static bool decode_frame(const void* buffer, size_t len, WsFrame& out) noexcept {
        const uint8_t* p = static_cast<const uint8_t*>(buffer);
        if (len < 2) return false;

        out.fin = (p[0] & 0x80);
        out.opcode = static_cast<WsOpcode>(p[0] & 0x0F);
        out.masked = (p[1] & 0x80);
        
        uint64_t pay_len = p[1] & 0x7F;
        size_t header_len = 2;

        if (pay_len == 126) {
            if (len < 4) return false;
            pay_len = (p[2] << 8) | p[3];
            header_len = 4;
        } else if (pay_len == 127) {
            if (len < 10) return false;
            pay_len = 0;
            for (int i = 0; i < 8; ++i) pay_len = (pay_len << 8) | p[2 + i];
            header_len = 10;
        }

        if (out.masked) {
            if (len < header_len + 4) return false;
            out.mask_key = (p[header_len] << 24) | (p[header_len + 1] << 16) | 
                           (p[header_len + 2] << 8) | p[header_len + 3];
            header_len += 4;
        }

        if (len < header_len + pay_len) return false;
        out.payload = p + header_len;
        out.payload_len = pay_len;
        return true;
    }
};

} // namespace snap
