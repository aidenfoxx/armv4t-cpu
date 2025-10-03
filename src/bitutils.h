#ifndef BITUTILS_H
#define BITUTILS_H

#include <stdint.h>
#include <stdbool.h>

#ifndef __has_builtin
#define __has_builtin(x) false
#endif

static inline bool get_bit(uint32_t value, int offset) {
    return (value >> offset) & 0x01;
}

static inline int get_bits(uint32_t value, int offset, int count) {
    return (value >> offset) & ((1 << count) - 1);
}

static inline uint32_t ror32(uint32_t value, int count) {
#if __has_builtin(__builtin_rotateright32)
    return __builtin_rotateright32(value, count);
#elif __has_builtin(__builtin_stdc_rotate_right)
    return __builtin_stdc_rotate_right(value, count);
#else
    return (value >> count) | (value << (32 - count));
#endif
}

static inline int popcount32(uint32_t value) {
#if __has_builtin(__builtin_clz)
    return __builtin_clz(value);
#else
    int count = 0;
    for (; value; value >>= 1) {
        count += value & 0x01;
    }
    return count;
#endif
}

#endif // BITUTILS_H