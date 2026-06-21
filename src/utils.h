#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __GNUC__
#define unreachable() (__builtin_unreachable())
#elif defined(_MSC_VER)
#define unreachable() (__assume(false))
#else
[[noreturn]] inline void unreachable_impl() {
}
#define unreachable() (unreachable_impl())
#endif

static inline bool get_bit(uint32_t value, int offset) {
    return (value >> offset) & 1;
}

static inline int get_bits(uint32_t value, int offset, int count) {
    return (value >> offset) & ((1u << count) - 1);
}

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

static inline uint32_t ror32(uint32_t value, int count) {
#if __has_builtin(__builtin_rotateright32)
    return __builtin_rotateright32(value, count);
#elif __has_builtin(__builtin_stdc_rotate_right)
    return __builtin_stdc_rotate_right(value, count);
#elif defined(_MSC_VER)
    return _rotr(value, count);
#else
    return (value >> count) | (value << (32 - count));
#endif
}

static inline int popcount32(uint32_t value) {
#if __has_builtin(__builtin_popcount)
    return __builtin_popcount(value);
#elif defined(_MSC_VER)
    return __popcnt(value);
#else
    int count = 0;
    for (; value != 0; count++) {
        value &= value - 1;
    }
    return count;
#endif
}

#endif // UTILS_H
