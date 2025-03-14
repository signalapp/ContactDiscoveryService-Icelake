// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_ORAM_H
#define CDS_ORAM_H 1

#include "util/util.h"
#include "util/statistics.h"

typedef struct oram oram;

typedef error_t (*accessor_func)(u64* rw_block_data, void* args);

/**
 * @brief Uses available memory to create a new recursive ORAM block store. Implements a modified version of the
 * Path ORAM algorithm (https://eprint.iacr.org/2013/280.pdf) with an ORAM-backed
 * position map.
 * 
 * @param available_bytes Bytes available to build this ORAM.
 * @param load_factor Ratio of number of ORAM blocks to number of bucket store leaves.
 *     Between 1.0 and 3.0 inclusive.
 * @param stash_overflow_size ize, in `block`s, of the overflow stash for this ORAM. 
 * @param getentropy entropy function used to randomize block positions.
 * @return oram* 
 */
oram* oram_create_for_available_mem(size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Creates a new recursive ORAM block store. Implements a modified version of the
 * Path ORAM algorithm (https://eprint.iacr.org/2013/280.pdf) with an ORAM-backed
 * position map.
 *
 * @param capacity_u64 The number of 64-bit integers the ORAM must hold. Actual
 * capacity will usually be higher.
 * @param stash_overflow_size Size, in `block`s, of the overflow stash for this ORAM. 
 * @param getentropy entropy function used to randomize block positions.
 * @return oram* Opaque pointer to an ORAM object. Must be destroyed using `oram_destroy`.
 */
oram *oram_create(size_t capacity_u64, size_t stash_overfow_size, entropy_func getentropy);

/**
 * @brief Frees resources held by the ORAM object. Is a no-op if the input is null.
 *
 */
void oram_destroy(oram *);

/**
 * @brief Deallocate blocks and clear all of the data in an ORAM
 *
 * @param oram
 */
void oram_clear(oram *oram);

/**
 * @brief Get the block size, in 64-bit integers, for an ORAM object.
 *
 * @param oram
 * @return size_t The number of 64-bit integers that fit in a block for this ORAM.
 */
size_t oram_block_size(const oram *);

/**
 * @brief Get the number of blocks this ORAM holds.
 *
 * @param oram
 * @return size_t number of blocks managed by the ORAM
 */
size_t oram_capacity_blocks(const oram *oram);


/**
 * @brief Put a block of data into an ORAM.
 *
 * @param block_id is the id of the block to retrieve.
 * @param data buffer of length `oram_block_size(oram*)` containing data to be written.
 * @return 0 if successful
 */
error_t oram_put(oram *, u64 block_id, const u64 data[]);

/**
 * @brief Overwrite part of an ORAM block and return previous value. This function is
 * not strictly necessary since a client can read a block, change part of it, then call
 * `oram_put` to store it. It is, however, an important optimization - especially for
 * `position_map` implementations where it allows us to reduce 2 ORAM calls to 1.
 *
 * @param block_id is the id of the block to retrieve.
 * @param start offset in the block where writing should start.
 * @param len number of u64s to write.
 * @param data data to write.
 * @param prev_data a buffer of length `len` to hold the data in the block before the put. May be 0.
 * @return 0 if successful
 */
error_t oram_put_partial(oram *, u64 block_id, size_t start, size_t len, u64 data[len], u64 *prev_data);

/**
 * @brief Apply an accessor function that can read/write a data block during an ORAM access. It is the responsibility
 * of the function to ensure that no information about the memory is leaked through memory access patterns.
 * 
 * @param oram 
 * @param block_id is the id of the block to retrieve.
 * @param accessor accessor function that will read and possibly write block data
 * @param accessor_args additional arguments for the accessor function, allowing this access to take input and produce computed output.
 * @return error_t 
 */
error_t oram_function_access(oram* oram, u64 block_id, accessor_func accessor, void* accessor_args);

/**
 * @brief Allocate an ORAM block and get the `block_id` for the new block.
 *
 * @return u64 The `block_id` of the allocated block. If the allocation fails, returns
 * UINT64_MAX.
 */
u64 oram_allocate_block(oram *);

/**
 * @brief Allocate multiple blocks at once.
 *
 * @param num_blocks Number of blocks to allocate.
 * @return u64 The `block_id` of the first allocated block. All blocks up to
 * the return value + `num_blocks` will be allocated (`return_val + 1`, `return_val + 2`, etc.)
 * If the allocation fails, returns UINT64_MAX.
 */
u64 oram_allocate_contiguous(oram *, size_t num_blocks);

/**
 * @brief Collect statistics about the health of this ORAM
 * 
 * @param oram 
 * @return const oram_statistics* Owned by this ORAM. Do not `free`.
 */
const oram_statistics* oram_report_statistics(oram* oram);

size_t oram_max_stash_size(const oram* oram);

/**
 * @brief Number of bytes needed to hold an ORAM with a given number of levels and blocks.
 * 
 * @param num_levels
 * @param num_blocks 
 * @return size_t 
 */
size_t oram_size_bytes(size_t num_levels, size_t num_blocks, size_t stash_overflow_size);

#ifdef IS_TEST

/**
 * @brief Read a block of data from an ORAM.
 *
 * @param block_id is the id of the block to retrieve.
 * @param buf buffer of length `oram_block_size(oram*)` where result will be written.
 * @return 0 if successful
 */
error_t oram_get(oram *, u64 block_id, u64 buf[]);

/**
 * @brief Obliviously read a part of an ORAM block.
 *
 * @param block_id is the id of the block to retrieve.
 * @param start offset in the block where reading should start.
 * @param len number of u64s to read.
 * @param data buffer where output data bill be written.
 * @return 0 if successful
 */
error_t oram_get_partial(oram *oram, u64 block_id, size_t start, size_t len, u64 data[len]);

void print_oram(const oram *oram);
void report_level_stats(oram *oram);
void run_path_oram_tests();
#endif // IS_TEST

#endif // CDS_ORAM_H
