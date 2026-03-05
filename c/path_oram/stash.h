// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_PATH_ORAM_STASH_H
#define CDS_PATH_ORAM_STASH_H 1

#include "util/util.h"
#include "bucket.h"
#include "position_map.h"
#include "tree_path.h"

typedef struct stash stash;

// Jasmin-exported layout functions (source of truth for struct stash layout)
extern size_t stash_sizeof_jazz(void);
extern size_t stash_blocks_offset_jazz(void);
extern size_t stash_path_blocks_offset_jazz(void);
extern size_t stash_overflow_blocks_offset_jazz(void);
extern size_t stash_num_blocks_offset_jazz(void);
extern size_t stash_overflow_capacity_offset_jazz(void);
extern size_t stash_bucket_occupancy_offset_jazz(void);
extern size_t stash_bucket_assignments_offset_jazz(void);

/**
 * @brief A `stash` is used internally by Path ORAM to cache blocks that are being moved
 * during an access and to possibly hold a small number of blocks between accesses when
 * there is no room in the buckets on the last path.
 * 
 * @param path_length Length of paths from leaf to root in the `bucket_store` associated with this `stash`'s `oram`.
 * @param overflow_size Capacity, in `block`s, of the overflow stash. The stash will be able to hold this many blocks between
 *        accesses.
 *
 * @return stash*
 */
stash *stash_create(size_t path_length, size_t overflow_size);
void stash_destroy(stash *stash);

/**
 * @brief Get a read-only view of the blocks for the last built path in the stash.
 * 
 * @param stash 
 * @return const block* 
 */
const block* stash_path_blocks(const stash* stash);

/**
 * @brief Clear all items from the stash
 * 
 * @param stash 
 * @return error_t 
 */
error_t stash_clear(stash* stash);

size_t stash_num_overflow_blocks(const stash* stash);

#ifdef IS_TEST

typedef enum {
    bucket_density_empty,
    bucket_density_sparse,
    bucket_density_dense,
    bucket_density_full
} bucket_density;

/**
 * @brief Test-only Jasmin function definitions for stash operations.
 */
extern void stash_add_path_bucket_jazz(stash* stash, bucket_store* bucket_store, u64 bucket_id, u64 target_block_id, block target[static 1]);
extern void stash_scan_overflow_for_target_jazz(stash* stash, u64 target_block_id, block target[static 1]);
extern error_t stash_add_block_jazz(stash* stash, block* new_block);

/**
 * @brief Test-only Jasmin function definitions for stash util.
 */
extern void cond_copy_block_jazz(bool cond, block* dst, const block* src);
extern void cond_swap_blocks_jazz(bool cond, block* a, block* b);
extern void odd_even_msort_jazz(block* blocks, u64* block_level_assignments, size_t lb, size_t ub);

void stash_print(const stash *stash);
int test_cond_cpy_block();
int test_oblv_sort();
int test_stash_insert_read();
int test_fill_stash();
int test_load_bucket_path_to_stash(bucket_density density);
#endif // IS_TEST
#endif // CDS_PATH_ORAM_STASH_H
