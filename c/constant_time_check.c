// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "util/util.h"

// This file is compiled into assembly, and we then look to make sure that
// an appropriately constant-time ternary operation is used during compilation,
// when building with the arguments we use to build enclave code.
uint64_t check_u64_ternary(bool bv, uint64_t a, uint64_t b) {
  return U64_TERNARY(bv, a, b);
}
