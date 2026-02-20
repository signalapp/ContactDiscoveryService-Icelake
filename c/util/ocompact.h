// Copyright 2026 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef __CDSI_UTIL_OCOMPACT_H
#define  __CDSI_UTIL_OCOMPACT_H

#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>

#ifdef IS_TEST
/**
 * @brief Obliviously compact an array of elements. This OCompact is specific to tests
 *
 * @param data
 * @param len number of elements in data
 * @param distances internal array. It must be allocated with size `len` before calling the function
 */
extern void ocompact_jazz(uint8_t* data, size_t len, size_t* distances);
#endif // IS_TEST

#endif //  __CDSI_UTIL_OCOMPACT_H
