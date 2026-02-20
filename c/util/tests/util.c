// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "util/util.h"
#include "util/log.h"
#include "util/tests.h"

#include <inttypes.h>
#include <sys/random.h>


int test_u64_ternary() {
  TEST_ASSERT(123 == U64_TERNARY(true, 123, 456));
  TEST_ASSERT(456 == U64_TERNARY(false, 123, 456));
  return 0;
}

int test_muluh() {
  u64 a = 1000*(1ul << 32) + 1001;
  u64 b = 1729*(1ul << 32) + 999;

  u64 uh = muluh64(a,b);
  TEST_ASSERT(uh == 1729000);

  for(size_t i = 0; i < 100; ++i) {
    getentropy(&a, sizeof(a));
    getentropy(&b, sizeof(b));
    uh = muluh64(a,b);

    u64 ah = (a >> 32);
    u64 bh = (b >> 32);
    u64 al = a %(1ul << 32);
    u64 bl = b % (1ul << 32);
    u64 lot_overflow = (((ah*bl) % (1ul << 32)) + ((al*bh) % ((1ul << 32))) + ((al*bl)>>32)) >> 32;
    u64 uh_direct = ah*bh + ((ah*bl >> 32) + (al*bh >> 32)) + lot_overflow;
    TEST_ASSERT(a = ah*(1ul << 32) + al);
    TEST_ASSERT(b = bh*(1ul << 32) + bl);
    TEST_ASSERT(al < (1ul << 32));
    TEST_ASSERT(bl < (1ul << 32));
    TEST_ASSERT(uh == uh_direct);
  }
  return err_SUCCESS;

}

int test_ct_div() {
  size_t num_divisors = 100;
  u64 d = 1729;
  u64 m_prime;

  size_t shift1;
  size_t shift2;

  u64 r;

  for(size_t divisor = 0; divisor < num_divisors; ++divisor){
    prep_ct_div(d, &m_prime, &shift1, &shift2);

    for(size_t i = 0; i < 10000; ++i) {
        getentropy(&r, sizeof(r));
        u64 q = ct_div(r, d, m_prime, shift1, shift2);
        if(q!= r/d) {
          TEST_LOG("i: %zu q: %lu r/d: %lu r: %lu d: %lu", i, q, r/d, r, d);
        }
        TEST_ASSERT(q == r/d);

    }

    // set d to be a new random u64
    getentropy(&d, sizeof(d));
  }
  return err_SUCCESS;
}

extern uint64_t floor_log2_jazz(uint64_t n);
extern uint64_t ceil_log2_jazz(uint64_t n);

int test_floor_log2() {
  // test against C reference implementation
  for (size_t n = 1; n <= 1024; ++n) {
    TEST_ASSERT(floor_log2_jazz(n) == floor_log2(n));
  }
  // powers of two
  for (size_t i = 0; i < 63; ++i) {
    uint64_t n = 1UL << i;
    TEST_ASSERT(floor_log2_jazz(n) == i);
  }
  // powers of two +/- 1
  for (size_t i = 2; i < 63; ++i) {
    uint64_t n = 1UL << i;
    TEST_ASSERT(floor_log2_jazz(n - 1) == i - 1);
    TEST_ASSERT(floor_log2_jazz(n + 1) == i);
  }
  return 0;
}

int test_ceil_log2() {
  // test against C reference implementation
  for (size_t n = 1; n <= 1024; ++n) {
    TEST_ASSERT(ceil_log2_jazz(n) == ceil_log2(n));
  }
  // powers of two: ceil_log2(2^i) == i
  for (size_t i = 0; i < 63; ++i) {
    uint64_t n = 1UL << i;
    TEST_ASSERT(ceil_log2_jazz(n) == i);
  }
  // powers of two + 1: ceil_log2(2^i + 1) == i + 1
  for (size_t i = 1; i < 63; ++i) {
    uint64_t n = (1UL << i) + 1;
    TEST_ASSERT(ceil_log2_jazz(n) == i + 1);
  }
  return 0;
}

int main(int argc, char** argv) {
  RUN_TEST(test_u64_ternary());
  RUN_TEST(test_muluh());
  RUN_TEST(test_ct_div());
  RUN_TEST(test_floor_log2());
  RUN_TEST(test_ceil_log2());
  return 0;
}
