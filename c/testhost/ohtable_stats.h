// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef __CDSI_TESTHOST_OHTABLE_STATS_H
#define __CDSI_TESTHOST_OHTABLE_STATS_H

#include "proto/cdsi.h"
#include "util/error.h"
#include "util/statistics.h"

/**
 * @brief decode protobuf enclave statistics
 * 
 * @param pbsize
 * @param pb 
 * @param stats array of `num_shards` `ohtable_statistics` structs. Results will be written here.
 * @param num_shards number of shards expected.
 */
error_t decode_statistics(size_t pbsize, uint8_t* pb, ohtable_statistics* stats, size_t num_shards);

#endif // __CDSI_TESTHOST_OHTABLE_STATS_H
