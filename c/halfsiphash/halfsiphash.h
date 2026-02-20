// Copyright 2026 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <string.h>

int halfsiphash(const void *in, const size_t inlen, const void *k, uint8_t *out,
                const size_t outlen);

#ifdef IS_TEST
uint64_t halfsiphash_u64(const void *in, const void *k);
#endif  // IS_TEST
