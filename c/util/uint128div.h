// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef __CDSI_UTIL_U128_H
#define __CDSI_UTIL_U128_H

#include <stdint.h>

uint64_t div128by64(__uint128_t u, uint64_t v);

#endif // __CDSI_UTIL_U128_H
