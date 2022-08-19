// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef _CDSI_RATELIMIT_H
#define _CDSI_RATELIMIT_H

#include <stdint.h>
#include <stdbool.h>

#include "util/util.h"
#include "proto/cdsi.h"

#define RATELIMIT_ENTROPY_SIZE 32
#define RATELIMIT_HASH_SIZE 32
#define RATELIMIT_VERSION 1
#define RATELIMIT_VERSION_SIZE 1  // We should only need a single byte to version this protocol, since it should not change often.
#define RATELIMIT_TOKEN_SIZE (RATELIMIT_VERSION_SIZE + RATELIMIT_ENTROPY_SIZE + RATELIMIT_HASH_SIZE)
#define RATELIMIT_TOKENHASH_SIZE 32

/** Set the global shared secret for rate limiting to the provided bytes. */
error_t ratelimit_set_shared_secret(
    size_t size,
    const uint8_t* bytes);

/** Merge-sort two sorted lists of e164s.
 *
 * Args:
 *  @param a_size,a_bytes First (sorted) e164 list, as concatenated big-endian u64s
 *  @param b_size,b_bytes Second (sorted) e164 list, as concatenated big-endian u64s
 *  @param out_size If successful, set to size of allocated *out_bytes
 *  @param out_bytes If successful, *out_bytes will point to an allocated buffer.
 *
 * @return
 *  err_SUCCESS: *out_bytes contains an allocated buffer with the merged, sorted
 *      concatenated big-endian u64s.
 *  err: *out_bytes unchanged, nothing allocated, error returned.
 */
error_t ratelimit_merge_e164s(
    size_t a_size,
    const uint8_t* a_bytes,
    size_t b_size,
    const uint8_t* b_bytes,
    size_t* out_size,
    uint8_t** out_bytes);

/** Returns true if the [s] bytes in [a] match the [s] bytes in [b]. */
bool ratelimit_constant_time_equal(
    const uint8_t* a,
    const uint8_t* b,
    size_t s);

/** Given entropy and two lists of e164s, return the hash portion of a token.
 *
 * Combines the shared secret, entropy, and merged e164s together into a hash.
 *
 * Args:
 *  @param e164s_a first set of e164s
 *  @param e164s_b second set of e164s
 *  @param token Pointer to RATELIMIT_TOKEN_SIZE bytes to put hash into.  Entropy
 *       _must_ already have been added to the token's prefix.
 */
error_t ratelimit_compute_token(
    const struct pbtools_bytes_t* e164s_a,
    const struct pbtools_bytes_t* e164s_b,
    uint8_t* token);

/** Hash a full token.
 *
 * Args:
 *  @param token RATELIMIT_TOKEN_SIZE bytes to hash
 *  @param hash RATELIMIT_TOKENHASH_SIZE bytes to write to
 */
void ratelimit_token_hash(
    const uint8_t* token,
    uint8_t* hash);

/** Validate that the token provided in [req] matches its contents. */
error_t ratelimit_validate_received_rate_limit_token(
    struct org_signal_cdsi_client_request_t* req);

#endif  // _CDSI_RATELIMIT_H
