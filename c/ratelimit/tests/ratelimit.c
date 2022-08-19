// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "ratelimit/ratelimit.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "util/util.h"

#define TEST_ASSERT(x) do { \
  if (!(x)) { \
    fprintf(stderr, "  FAILED at %s:%d\n\t%s\n", __FILE__, __LINE__, #x); \
    exit(1); \
  } \
} while (0)

#define TEST_ERR(x) TEST_ASSERT(err_SUCCESS == (x))

static void test_set_shared_secret() {
  uint8_t secret[] = {1,2,3};
  TEST_ERR(ratelimit_set_shared_secret(sizeof(secret), secret));
}

static void test_roundtrip() {
  uint8_t prev_e164s[] = {
    0,0,0,0,0,0,0,1,
    0,0,0,0,0,0,0,4,
    0,0,0,0,0,0,0,7,
  };
  uint8_t discard_e164s[] = {
    0,0,0,0,0,0,0,2,
    0,0,0,0,0,0,0,5,
    0,0,0,0,0,0,0,8,
  };
  uint8_t new_e164s[] = {
    0,0,0,0,0,0,0,3,
    0,0,0,0,0,0,0,6,
    0,0,0,0,0,0,0,9,
  };

  uint8_t workspace[128];
  uint8_t token_space[RATELIMIT_TOKEN_SIZE] = {RATELIMIT_VERSION, /* entropy */ 1, 2, 3, 4};
  struct org_signal_cdsi_client_request_t* req =
      org_signal_cdsi_client_request_new(workspace, sizeof(workspace));
  req->prev_e164s.size = sizeof(prev_e164s);
  req->prev_e164s.buf_p = prev_e164s;
  req->new_e164s.size = sizeof(new_e164s);
  req->new_e164s.buf_p = new_e164s;
  req->discard_e164s.size = sizeof(discard_e164s);
  req->discard_e164s.buf_p = discard_e164s;
  req->token.size = sizeof(token_space);
  req->token.buf_p = token_space;

  TEST_ERR(ratelimit_compute_token(
      &req->prev_e164s,
      &req->discard_e164s,
      req->token.buf_p));
  
  uint8_t expected_token[] = {
      // VERSION
      RATELIMIT_VERSION,
      // ENTROPY
      0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // HASH
      0x51, 0xe6, 0xb1, 0xf5, 0x64, 0xaf, 0x04, 0x79,
      0x35, 0xdd, 0x2d, 0xe1, 0x70, 0xbd, 0x71, 0x36,
      0x71, 0x09, 0x2d, 0x48, 0x90, 0x22, 0x60, 0x3b,
      0x25, 0x4c, 0xcc, 0x7d, 0xdc, 0x4f, 0x03, 0x74,
  };
  for (size_t i = 0; i < req->token.size; i++) {
    fprintf(stderr, "0x%02x, ", req->token.buf_p[i]);
  }
  fprintf(stderr, "\n");
  TEST_ASSERT(!memcmp(expected_token, req->token.buf_p, RATELIMIT_TOKEN_SIZE));

  TEST_ERR(ratelimit_validate_received_rate_limit_token(req));
}

static void test_merge() {
  uint8_t e0[] = {
  };
  uint8_t e1[] = {
    0,0,0,0,0,0,0,3,
  };
  uint8_t e3a[] = {
    0,0,0,0,0,0,0,1,
    0,0,0,0,0,0,0,4,
    0,0,0,0,0,0,0,7,
  };
  uint8_t e3b[] = {
    0,0,0,0,0,0,0,2,
    0,0,0,0,0,0,0,5,
    0,0,0,0,0,0,0,8,
  };

  uint8_t* m = 0;
  size_t m_size = 0;

  TEST_ERR(ratelimit_merge_e164s(sizeof(e0), e0, sizeof(e3a), e3a, &m_size, &m));
  {
    uint8_t expected[] = {
        0,0,0,0,0,0,0,1,
        0,0,0,0,0,0,0,4,
        0,0,0,0,0,0,0,7,
    };
    TEST_ASSERT(m_size == sizeof(expected));
    TEST_ASSERT(!memcmp(m, expected, sizeof(expected)));
    free(m);
  };

  TEST_ERR(ratelimit_merge_e164s(sizeof(e3a), e3a, sizeof(e0), e0, &m_size, &m));
  {
    uint8_t expected[] = {
        0,0,0,0,0,0,0,1,
        0,0,0,0,0,0,0,4,
        0,0,0,0,0,0,0,7,
    };
    TEST_ASSERT(m_size == sizeof(expected));
    TEST_ASSERT(!memcmp(m, expected, sizeof(expected)));
    free(m);
  };

  TEST_ERR(ratelimit_merge_e164s(sizeof(e1), e1, sizeof(e3b), e3b, &m_size, &m));
  {
    uint8_t expected[] = {
        0,0,0,0,0,0,0,2,
        0,0,0,0,0,0,0,3,
        0,0,0,0,0,0,0,5,
        0,0,0,0,0,0,0,8,
    };
    TEST_ASSERT(m_size == sizeof(expected));
    TEST_ASSERT(!memcmp(m, expected, sizeof(expected)));
    free(m);
  };

  TEST_ERR(ratelimit_merge_e164s(sizeof(e3a), e3a, sizeof(e3a), e3a, &m_size, &m));
  {
    uint8_t expected[] = {
        0,0,0,0,0,0,0,1,
        0,0,0,0,0,0,0,1,
        0,0,0,0,0,0,0,4,
        0,0,0,0,0,0,0,4,
        0,0,0,0,0,0,0,7,
        0,0,0,0,0,0,0,7,
    };
    TEST_ASSERT(m_size == sizeof(expected));
    TEST_ASSERT(!memcmp(m, expected, sizeof(expected)));
    free(m);
  };

  TEST_ERR(ratelimit_merge_e164s(sizeof(e3a), e3a, sizeof(e3b), e3b, &m_size, &m));
  {
    uint8_t expected[] = {
        0,0,0,0,0,0,0,1,
        0,0,0,0,0,0,0,2,
        0,0,0,0,0,0,0,4,
        0,0,0,0,0,0,0,5,
        0,0,0,0,0,0,0,7,
        0,0,0,0,0,0,0,8,
    };
    TEST_ASSERT(m_size == sizeof(expected));
    TEST_ASSERT(!memcmp(m, expected, sizeof(expected)));
    free(m);
  };

  TEST_ERR(ratelimit_merge_e164s(sizeof(e3b), e3b, sizeof(e3a), e3a, &m_size, &m));
  {
    uint8_t expected[] = {
        0,0,0,0,0,0,0,1,
        0,0,0,0,0,0,0,2,
        0,0,0,0,0,0,0,4,
        0,0,0,0,0,0,0,5,
        0,0,0,0,0,0,0,7,
        0,0,0,0,0,0,0,8,
    };
    TEST_ASSERT(m_size == sizeof(expected));
    TEST_ASSERT(!memcmp(m, expected, sizeof(expected)));
    free(m);
  };
}

static void test_constant_eq() {
  uint8_t input_a[] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t input_b[] = {1, 2, 3, 4, 0xff, 6, 7, 8};
  uint8_t input_c[] = {1, 2, 3, 4, 5, 6, 7, 0xff};
  TEST_ASSERT(sizeof(input_a) == sizeof(input_b));
  TEST_ASSERT(sizeof(input_a) == sizeof(input_c));
  TEST_ASSERT(ratelimit_constant_time_equal(input_a, input_a, sizeof(input_a)));
  TEST_ASSERT(!ratelimit_constant_time_equal(input_a, input_b, sizeof(input_a)));
  TEST_ASSERT(!ratelimit_constant_time_equal(input_a, input_c, sizeof(input_a)));
}

void bitonic_sort_uint64s(u64* data, size_t n);

static void test_bitonic_sort_uint64s() {
  int f = open("/dev/urandom", 0);
  TEST_ASSERT(f > 0);
  for (size_t iter = 0; iter < 16; iter++) {
    uint8_t buf[2];
    TEST_ASSERT(2 == read(f, buf, 2));
    size_t size = (((size_t) buf[1]) << 8) + buf[0];
    uint64_t* to_sort = calloc(size, sizeof(uint64_t));
    size_t bytes = size * sizeof(uint64_t);
    char* ptr = (char*) to_sort;
    while (bytes > 0) {
      int r = read(f, ptr, bytes);
      TEST_ASSERT(r > 0);
      ptr += r;
      bytes -= r;
    }
    int64_t start = time_micros();
    bitonic_sort_uint64s(to_sort, size);
    int64_t duration = time_micros() - start;
    TEST_LOG("size %ld in %ld micros", size, duration);
    uint64_t last = 0;
    for (size_t i = 0; i < size; i++) {
      TEST_ASSERT(last <= to_sort[i]);
      last = to_sort[i];
    }
    free(to_sort);
  }
}

#define RUN_TEST(x) do { \
  fprintf(stderr, "\nRunning test: %s...\n", #x); \
  x(); \
  fprintf(stderr, "  SUCCESS\n"); \
} while (0)

int main(int argc, char** argv) {
  RUN_TEST(test_set_shared_secret);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_merge);
  RUN_TEST(test_constant_eq);
  RUN_TEST(test_bitonic_sort_uint64s);
  return 0;
}
