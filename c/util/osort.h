// Copyright 2026 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef _CDSI_UTIL_OSORT_H
#define _CDSI_UTIL_OSORT_H

#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>

#ifdef IS_TEST
/**
 * @brief Obliviously sorts an array of elements. This OSort is specific to tests
 *
 * @param data
 * @param lb lower bound index (inclusive) from where to start sorting
 * @param ub upper bound index (exclusive) until where to sort
 */
extern void osort_jazz(uint8_t* data, size_t lb, size_t ub);
#endif // IS_TEST

#endif // _CDSI_UTIL_OSORT_H
