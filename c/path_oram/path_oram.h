// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_ORAM_H
#define CDS_ORAM_H 1

#include "util/util.h"
#include "util/statistics.h"

typedef struct oram oram;

// Forward declarations for Jasmin init function
typedef struct bucket_store bucket_store;
typedef struct position_map position_map;
typedef struct stash stash;
typedef struct tree_path tree_path;

// Jasmin-exported layout functions (source of truth for struct oram layout)
extern size_t oram_sizeof_jazz(void);
extern size_t oram_bucket_store_offset_jazz(void);
extern size_t oram_position_map_offset_jazz(void);
extern size_t oram_stash_offset_jazz(void);
extern size_t oram_path_offset_jazz(void);
extern size_t oram_capacity_blocks_offset_jazz(void);
extern size_t oram_statistics_offset_jazz(void);
oram* oram_create_120G_16shards(entropy_func getentropy);
oram* oram_create_120G_16shards_posmap(entropy_func getentropy);
oram* oram_create_depth16(entropy_func getentropy);
oram* oram_create_depth16_posmap(entropy_func getentropy);

typedef error_t (*accessor_func)(u64* rw_block_data, void* args);

/**
 * @brief Frees resources held by the ORAM object. Is a no-op if the input is null.
 *
 */
void oram_destroy(oram *);

/**
 * @brief Clear all of the data in an ORAM
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
 * @brief Collect statistics about the health of this ORAM
 * 
 * @param oram 
 * @return const oram_statistics* Owned by this ORAM. Do not `free`.
 */
const oram_statistics* oram_report_statistics(oram* oram);

size_t oram_max_stash_size(const oram* oram);

/**
 * @brief Put a block of data into an ORAM.
 *
 * @param block_id is the id of the block to retrieve.
 * @param data buffer of length `oram_block_size(oram*)` containing data to be written.
 * @return 0 if successful
 */
error_t oram_put(oram *, u64 block_id, const u64 data[]);

/**
 * @brief Put a block of data into ORAM position map.
 *
 * @param block_id is the id of the block to retrieve.
 * @param data buffer of length `oram_block_size(oram*)` containing data to be written.
 * @return 0 if successful
 */
error_t oram_position_map_put(oram *, u64 block_id, const u64 data[]);

/**
 * @brief Apply `get_record_accessor` to a data block during an ORAM access.
 * 
 * @param oram 
 * @param block_id is the id of the block to retrieve.
 * @param accessor_args additional arguments for the accessor.
 * @return 0 if successful
 */
error_t oram_function_access_get(oram* oram, u64 block_id, void* accessor_args);

/**
 * @brief Apply `robinhood_accessor` to a data block during an ORAM access.
 * 
 * @param oram 
 * @param block_id is the id of the block to retrieve.
 * @param accessor_args additional arguments for the accessor.
 * @return 0 if successful
 */
error_t oram_function_access_put(oram* oram, u64 block_id, void* accessor_args);

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
