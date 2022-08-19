// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef LIBORAM_UTIL_H
#define LIBORAM_UTIL_H 1

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "int_types.h"
#include "error.h"
#include "log.h"
#include "uint128div.h"

inline static size_t floor_log2(size_t n)
{
    size_t result = 0;
    while (n > 1)
    {
        n >>= 1;
        result += 1;
    }
    return result;
}

inline static size_t ceil_log2(size_t n)
{
    size_t floor = floor_log2(n);
    return n > ((size_t)1) << floor ? floor + 1 : floor;
}


typedef int (*entropy_func)(void *buf, size_t len);

#define max(a, b) \
    ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define CHECK(x)                                                                     \
    do                                                                               \
    {                                                                                \
        if (!(x))                                                                    \
        {                                                                            \
            fprintf(stderr, "CHECK(%s) failure at %s:%d\n", #x, __FILE__, __LINE__); \
            abort();                                                                 \
        }                                                                            \
    } while (0)

// Compiler-time assertion.  This generates errors at compile-time if constant expressions
// evaluate to false.  Example failure:
//
//    /home/gram/src/ContactDiscoveryService-Icelake-Private/c/util/util.h:73:1: error: 'compile_time_assert_failure__0' declared as an array with a negative size
//    COMPILE_TIME_ASSERT(1 == 2);
//    ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//    /home/gram/src/ContactDiscoveryService-Icelake-Private/c/util/util.h:70:121: note: expanded from macro 'COMPILE_TIME_ASSERT'
//    #define COMPILE_TIME_ASSERT(boolval) typedef int _COMPILE_TIME_ASSERT_GLUE(compile_time_assert_failure__, __COUNTER__) [(boolval) ? 0 : -1];
#define _COMPILE_TIME_ASSERT_GLUE(a, b) _COMPILE_TIME_ASSERT_GLUE2(a, b)
#define _COMPILE_TIME_ASSERT_GLUE2(a, b) a ## b
#define COMPILE_TIME_ASSERT(boolval) typedef int _COMPILE_TIME_ASSERT_GLUE(compile_time_assert_failure__, __COUNTER__) [(boolval) ? 0 : -1];

// Constant-time ternary value.  Requires that true == 1 and false == 0.
//   U64_TERNARY(true, 123, 456) -> 123
//   U64_TERNARY(false, 123, 456) -> 456
// Right now, on x86 hardware with -O3, this compiles to the same opcodes as (b ? t : f).
// However, this should compile in any configuration and with any optimization to something
// that's constant time and doesn't have jumps in it.  This hasn't been extensively tested,
// but we have confirmed that it does work without -O3, whereas the ternarly (b ? t : f)
// without -O3 compiles to a jne instruction.
COMPILE_TIME_ASSERT((uint64_t) true == 1);
COMPILE_TIME_ASSERT((uint64_t) false == 0);
#define U64_TERNARY(boolval, u64true, u64false) \
    (((0 - ((boolval) != false)) & u64true) | (((UINT64_MAX + ((boolval) != false)) & u64false)))


/**
 * @brief Obliviously copy a `u64` to a destination if a condition is true. Here "obliviously" means
 * that the same instructions are executed whether the condition is true or false.
 * 
 * @param cond The copy will happen when this is true.
 * @param dest Copy to this destination.
 * @param src  Location of value to copy.
 */
static inline void cond_obv_cpy_u64(bool cond, u64 *dest, const u64 *src) {
    *dest = U64_TERNARY(cond, *src, *dest);
}

static inline void cond_obv_swap_u64(bool cond, u64 *a, u64 *b) {
    u64 tmp;
    cond_obv_cpy_u64(cond, &tmp, a);
    cond_obv_cpy_u64(cond, a, b);
    cond_obv_cpy_u64(cond, b, &tmp);

}

static inline u64 first_pow2_leq(u64 n) {
    // precondition: n > 0
    size_t msb = 64 - __builtin_clzll(n);
    return 1UL << (msb - 1);
}

/**
 * @brief Return the high bits of the product of two 64-bit integers. That is,
 * returns floor((a*b)/2^64)
 * 
 * @param a 
 * @param b 
 * @return u64 
 */
static inline u64 muluh64(u64 a, u64 b) {
    return ((__uint128_t)a*b) >> 64;

}

/**
 * @brief Precomputes constants for constant-time division method in Granlund and Montgomery,
 * "Division by Invariant Integers using Multiplication" (https://gmplib.org/~tege/divcnst-pldi94.pdf).
 * 
 * *This function is NOT constant time and should only be used for non-secret divisors, `d`.
 * 
 * @param d 
 * @param m_prime 
 * @param shift1 
 * @param shift2 
 */
static inline void prep_ct_div(u64 d, u64* m_prime, size_t* shift1, size_t* shift2) {
    size_t l = ceil_log2(d);
    // division is non-constant time, but this is only done once during setup,
    // not when operating on secret data
    __uint128_t dividend = ((__uint128_t)1 << 64)*((1ul << l) - d);
    uint64_t q;
    q = div128by64(dividend, d);
    *m_prime = 1 + q;
    *shift1 = l < 1 ? l : 1;
    *shift2 = (l-1) > 0 ? (l-1) : 0;
}

/**
 * @brief Constant-time division method of Granlund and Montgomery,
 * "Division by Invariant Integers using Multiplication" (https://gmplib.org/~tege/divcnst-pldi94.pdf).
 * 
 * @param n 
 * @param d 
 * @param m_prime 
 * @param shift1 
 * @param shift2 
 * @return u64 
 */
static inline u64 ct_div(u64 n, u64 d, u64 m_prime, size_t shift1, size_t shift2) {
    u64 t1 = muluh64(m_prime, n);
    return (t1 + ((n-t1) >> shift1)) >> shift2;
}

/**
 * @brief Constant time modulo operator for invariant divisor using `ct_div` based on Granlund and Montgomery,
 * "Division by Invariant Integers using Multiplication" (https://gmplib.org/~tege/divcnst-pldi94.pdf).
 * 
 * @param n 
 * @param d 
 * @param m_prime 
 * @param shift1 
 * @param shift2 
 * @return u64 
 */
static inline u64 ct_mod(u64 n, u64 d, u64 m_prime, size_t shift1, size_t shift2) {
    u64 q = ct_div(n,d,m_prime, shift1, shift2);
    return n - q*d;
}

#endif // LIBORAM_UTIL_H
