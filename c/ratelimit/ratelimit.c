// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "ratelimit.h"

#include <endian.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "util/util.h"
#include "proto/cdsi.h"
#include "noise-c/src/crypto/sha2/sha256.h"

static unsigned char g_shared_secret_bytes[64] = {0};
static size_t g_shared_secret_size = 0;

static void bitonic_merge_u64s_recurse(u64* data, size_t lb, size_t ub, bool direction) {
    size_t n = ub - lb;
    if(n > 1) {
        size_t pow2 = first_pow2_leq(n);
        if(pow2 == n) pow2 >>= 1;
        for(size_t i = lb; i < ub - pow2; ++i) {
            bool cond = direction == data[i] < data[i+pow2];
            cond_obv_swap_u64(cond, data + i, data + i + pow2);
        }

        // Note that at this point, since the input was bitonic, everything in the array with
        // index >= lb + pow2 has a "larger" value (relative to `direction`) than the entries
        // with index < lb + pow2. Also, both the upper and lower part of the array are bitonic
        // subarrays.
        bitonic_merge_u64s_recurse(data, lb, lb + pow2, direction);
        bitonic_merge_u64s_recurse(data, lb + pow2, ub, direction);
    }
}
static void bitonic_sort_u64s_recurse(u64* data, size_t lb, size_t ub, bool direction) {
    size_t n = ub - lb;
    if(n > 1) {
        size_t half_n = n>>1;
        bitonic_sort_u64s_recurse(data, lb, lb + half_n, !direction);
        bitonic_sort_u64s_recurse(data, lb + half_n, ub, direction);
        bitonic_merge_u64s_recurse(data, lb, ub, direction);
    }
}
#ifndef IS_TEST
static
#endif
void bitonic_sort_uint64s(u64* data, size_t size) {
  bitonic_sort_u64s_recurse(data, 0, size, false);
}

error_t ratelimit_set_shared_secret(
    size_t size,
    const uint8_t* bytes) {
  ASSERT_ERR(
      size >= 0 && size <= sizeof(g_shared_secret_bytes),
      err_ENCLAVE__LOADPB__SECRET_TOO_LARGE);
  memcpy(g_shared_secret_bytes, bytes, size);
  g_shared_secret_size = size;
  return err_SUCCESS;
}

error_t ratelimit_merge_e164s(
    size_t a_size,
    const uint8_t* a_bytes,
    size_t b_size,
    const uint8_t* b_bytes,
    size_t* out_size,
    uint8_t** out_bytes) {
  ASSERT_ERR(a_size % sizeof(uint64_t) == 0, err_ENCLAVE__RATELIMIT__INVALID_E164S);
  ASSERT_ERR(b_size % sizeof(uint64_t) == 0, err_ENCLAVE__RATELIMIT__INVALID_E164S);
  const uint64_t* a = (const uint64_t*) a_bytes;
  const uint64_t* b = (const uint64_t*) b_bytes;
  const size_t a_u64s = a_size / sizeof(uint64_t);
  const size_t b_u64s = b_size / sizeof(uint64_t);
  uint64_t* out;
  RETURN_IF_ERROR(MALLOCZ_SIZE(out, a_size+b_size));
  // Concatenate [a] and [b] into [out].
  memcpy(out, a, a_size);
  memcpy(out+a_u64s, b, b_size);
  // Sort [out].
  bitonic_sort_uint64s(out, a_u64s+b_u64s);
  *out_size = a_size + b_size;
  *out_bytes = (uint8_t*) out;
  return err_SUCCESS;
}

bool ratelimit_constant_time_equal(const uint8_t* a, const uint8_t* b, size_t s) {
  uint8_t c = 0;
  while (s--) {
    c |= *a++ ^ *b++;
  }
  return c == 0;
}

static void xor_bytes(unsigned char* data, unsigned char xor, size_t size) {
  for (size_t i = 0; i < size; i++) {
    data[i] ^= xor;
  }
}

error_t ratelimit_compute_token(
    const struct pbtools_bytes_t* e164s_a,
    const struct pbtools_bytes_t* e164s_b,
    uint8_t* token) {
  uint8_t* merged = NULL;
  size_t merged_size = 0;
  RETURN_IF_ERROR(ratelimit_merge_e164s(
      e164s_a->size, e164s_a->buf_p,
      e164s_b->size, e164s_b->buf_p,
      &merged_size, &merged));

  // HMAC_SHA256(key=g_shared_secret, input=version..entropy..merged)
  sha256_context_t sha;
  sha256_reset(&sha);
  uint8_t pad[64];
  memset(pad, 0, sizeof(pad));
  memcpy(pad, g_shared_secret_bytes, g_shared_secret_size);
  xor_bytes(pad, 0x36, sizeof(pad));
  sha256_update(&sha, pad, sizeof(pad));
  sha256_update(&sha, token, RATELIMIT_VERSION_SIZE+RATELIMIT_ENTROPY_SIZE);
  sha256_update(&sha, merged, merged_size);
  free(merged);
  uint8_t hash1[32];
  sha256_finish(&sha, hash1);
  sha256_reset(&sha);
  memset(pad, 0, sizeof(pad));
  memcpy(pad, g_shared_secret_bytes, g_shared_secret_size);
  xor_bytes(pad, 0x5c, sizeof(pad));
  sha256_update(&sha, pad, sizeof(pad));
  sha256_update(&sha, hash1, sizeof(hash1));
  sha256_finish(&sha, token+RATELIMIT_VERSION_SIZE+RATELIMIT_ENTROPY_SIZE);
  return err_SUCCESS;
}

void ratelimit_token_hash(
    const uint8_t* token,
    uint8_t* hash) {
  sha256_context_t sha;
  sha256_reset(&sha);
  sha256_update(&sha, token, RATELIMIT_TOKEN_SIZE);
  sha256_finish(&sha, hash);
}

error_t ratelimit_validate_received_rate_limit_token(
    struct org_signal_cdsi_client_request_t* req) {
  if (req->token.size == 0) {
    if (req->prev_e164s.size > 0) { return err_ENCLAVE__RATELIMIT__NO_TOKEN; }
    return err_SUCCESS;
  }
  if (req->token.size != RATELIMIT_TOKEN_SIZE) {
    return err_ENCLAVE__RATELIMIT__INVALID_TOKEN_FORMAT;
  }
  if (req->token.buf_p[0] != RATELIMIT_VERSION) {
    return err_ENCLAVE__RATELIMIT__UNSUPPORTED_VERSION;
  }
  const uint8_t* token = req->token.buf_p;

  uint8_t computed_token[RATELIMIT_TOKEN_SIZE];
  // Copy in version/entropy.
  memcpy(computed_token, token, RATELIMIT_VERSION_SIZE+RATELIMIT_ENTROPY_SIZE);
  RETURN_IF_ERROR(ratelimit_compute_token(
      &req->prev_e164s, &req->discard_e164s, computed_token));
  ASSERT_ERR(
      ratelimit_constant_time_equal(computed_token, token, RATELIMIT_TOKEN_SIZE),
      err_ENCLAVE__RATELIMIT__INVALID_TOKEN);
  return err_SUCCESS;
}
