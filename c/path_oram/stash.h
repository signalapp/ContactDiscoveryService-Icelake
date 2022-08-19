// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_PATH_ORAM_STASH_H
#define CDS_PATH_ORAM_STASH_H 1

#include "util/util.h"
#include "bucket.h"
#include "position_map.h"
#include "tree_path.h"

typedef struct stash stash;

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
 * @brief Loads a bucket from a `bucket_store` into the appropriate level of the `path_stash`. If the block with
 * the `target_id` is present in the bucket, it is obliviously swapped into the `target` block, leaving the space in the
 * `path_stash` empty.
 * 
 * @param stash 
 * @param bucket_store 
 * @param bucket_id ID of bucket to load into the stash
 * @param target_block_id ID of block being retrieved
 * @param target Output buffer - if the block with ID `target_id` is present in the bucket, the block will be written here instead of 
 *               in the stash.
 */
void stash_add_path_bucket(stash* stash, bucket_store* bucket_store, u64 bucket_id, u64 target_block_id, block target[static 1]);
/**
 * @brief Linearly scans `stash->overflow` and if it finds a block with ID equal to `target_block_id` it obliviously swaps 
 *        this block into `target`. Due to the precondition discussed below, this swap will always place an empty block in the
 *        overflow stash when it occurs.
 * 
 *        Precondition: Either `target[0]` is an empty block or there is no block with ID equal to `target_block_id` in the
 *        overflow stash. This can be enforced because of the following invariant: between accesses every block is stored 
 *        either in the `bucket_store` or in `stash->overflow`, but not both. If we read a path from the `bucket_store` using
 *        `stash_add_path_bucket` and find the target, then we know the target is not empty but is not in `stash->overflow`. 
 *        On the other hand if we do not find the target, then our target is empty. Either way, our precondition will be met.
 * 
 * @param stash 
 * @param target_block_id ID of block to find
 * @param target Output buffer - if the block with ID `target_id` is present in `stash->overflow`, the block will be swapped into
 *               target, removing it from `stash->overflow`.
 */
void stash_scan_overflow_for_target(stash* stash, u64 target_block_id, block target[static 1]);
/**
 * @brief Adds a block to the overflow blocks for a stash.
 *        Precondition: there is no block with ID `new_block->id` anywhere in the stash - neither in `stash->path_stash` 
 *        nor `stash->overflow`.
 * @param stash 
 * @param new_block The block to add to `stash->overflow`
 * @return error_t 
 */
error_t stash_add_block(stash* stash, block* new_block);

/**
 * @brief Assigns all blocks in the stush to buckets on the current path or to the overflow stash. Performs
 *        an oblivious sort on these blocks so that (1) they can be serially read and stored in buckets and (2) 
 *        the remaining overflow stash is compacted, with all non-empty blocks in the lowest indices.
 * 
 * @param stash 
 * @param path 
 */
void stash_build_path(stash* stash, const tree_path* path);

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

size_t stash_size_bytes(size_t path_length, size_t overflow_size);

#ifdef IS_TEST

typedef enum {
    bucket_density_empty,
    bucket_density_sparse,
    bucket_density_dense,
    bucket_density_full
} bucket_density;

void stash_print(const stash *stash);
int test_cond_cpy_block();
int test_oblv_sort();
int test_stash_insert_read();
int test_fill_stash();
int test_load_bucket_path_to_stash(bucket_density density);
#endif // IS_TEST
#endif // CDS_PATH_ORAM_STASH_H
