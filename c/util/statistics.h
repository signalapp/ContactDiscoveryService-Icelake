// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef _CDSI_STATISTICS_H
#define _CDSI_STATISTICS_H

#include "int_types.h"


typedef struct {
    size_t recursion_depth;
    size_t access_count;
    size_t stash_overflow_count;
    size_t max_stash_overflow_count;
    size_t sum_stash_overflow_count;
    double stash_overflow_ema10k; // exponential moving average of stash_overflow_count with weight half-life of 10000 accesses 
    size_t posmap_stash_overflow_count;
    size_t posmap_max_stash_overflow_count;
    size_t posmap_sum_stash_overflow_count;
    double posmap_stash_overflow_ema10k; // exponential moving average of posmap_stash_overflow_count with weight half-life of 10000 accesses 
} oram_statistics;

typedef struct {
    size_t max_trace_length;
    size_t total_displacement;
    size_t num_items;
    size_t capacity;

    // ORAM statistics
    size_t oram_recursion_depth;
    size_t oram_access_count;
    size_t stash_overflow_count;
    size_t max_stash_overflow_count;
    size_t sum_stash_overflow_count;
    double stash_overflow_ema10k; // exponential moving average of stash_overflow_count with weight half-life of 10000 accesses 
    size_t posmap_stash_overflow_count;
    size_t posmap_max_stash_overflow_count;
    size_t posmap_sum_stash_overflow_count;
    double posmap_stash_overflow_ema10k; // exponential moving average of posmap_stash_overflow_count with weight half-life of 100 accesses 

} ohtable_statistics;

#endif // _CDSI_STATISTICS_H
