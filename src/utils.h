#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

#ifdef __GNUC__
#define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

#ifdef __GNUC__
#define unreachable() (__builtin_unreachable())
#elif defined(_MSC_VER)
#define unreachable() (__assume(false))
#else
[[noreturn]] inline void unreachable_impl() {
}
#define unreachable() (unreachable_impl())
#endif

static inline bool get_bit(uint32_t value, uint32_t offset) {
    return (value >> offset) & 1;
}

static inline uint32_t get_bits(uint32_t value, uint32_t offset, uint32_t count) {
    return (value >> offset) & ((1u << count) - 1);
}

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

static inline int popcount32(uint32_t value) {
#if __has_builtin(__builtin_popcount)
    return __builtin_popcount(value);
#else
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        count++;
    }
    return count;
#endif
}

#endif // UTILS_H
