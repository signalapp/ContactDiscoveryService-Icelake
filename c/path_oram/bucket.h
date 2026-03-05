// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_PATH_ORAM_BUCKET_H
#define CDS_PATH_ORAM_BUCKET_H 1

#include "util/util.h"
#include <stdbool.h>

// 4 KB Page
// This is the size of an SGX EPC page. We can vary this parameter as we are tuning performance.
// Informal tests saw worse performance for 8192 and 16384 byte buckets.
#define ENCRYPTED_BUCKET_SIZE 4096

// In SGX, the MEE encrypts for us so we do not need AE overhead. Ciphertext size = plaintext size.
#define DECRYPTED_BUCKET_SIZE ENCRYPTED_BUCKET_SIZE
#define BLOCKS_PER_BUCKET 3
#define DECRYPTED_BLOCK_SIZE (DECRYPTED_BUCKET_SIZE / BLOCKS_PER_BUCKET)
#define DECRYPTED_BLOCK_SIZE_QWORDS (DECRYPTED_BLOCK_SIZE / 8)

#define UNROUNDED_BLOCK_DATA_SIZE_BYTES (DECRYPTED_BLOCK_SIZE - 16)
#define BLOCK_DATA_SIZE_QWORDS (UNROUNDED_BLOCK_DATA_SIZE_BYTES / 8)
#define BLOCK_DATA_SIZE_BYTES (BLOCK_DATA_SIZE_QWORDS * 8)

#define EMPTY_BLOCK_ID UINT64_MAX

typedef struct bucket_store bucket_store;

// Jasmin-exported layout functions (source of truth for struct bucket_store layout)
extern size_t bucket_store_sizeof_jazz(void);
extern size_t bucket_store_size_bytes_offset_jazz(void);
extern size_t bucket_store_data_offset_jazz(void);

// Create a path ORAM bucket store with capacity for a tree with `num_levels` levels,
// i.e. 2^num_levels - 1 tree nodes and 2^(num_levels - 1) leaf nodes/pathORAM positions.
bucket_store *bucket_store_create(size_t num_levels);
void bucket_store_destroy(bucket_store *bucket_store);

void bucket_store_clear(bucket_store *bucket_store);

typedef struct block block;
struct block
{
    u64 id;
    u64 position;
    u64 data[BLOCK_DATA_SIZE_QWORDS];
};

// The number of 64-bit ints the block will hold
size_t bucket_store_block_data_size(bucket_store *bucket_store);

bool block_is_empty(block block);

#ifdef IS_TEST
/**
 * @brief Read all blocks, including empty ones, from a bucket into a buffer
 * 
 * @param bucket_store 
 * @param bucket_id ID of bucket to read
 * @param bucket_data buffer where blocks will be written
 */
extern void bucket_store_read_bucket_blocks_jazz(bucket_store *bucket_store, u64 bucket_id, block bucket_data[BLOCKS_PER_BUCKET]);

/**
 * @brief Write a full set of blocks to a buffer. Must write `BLOCKS_PER_BUCKET` blocks, partial writes
 *        will produce undefined behavior. Pad with empty blocks if needed.
 * 
 * @param bucket_store 
 * @param bucket_id ID of bucket to write
 * @param bucket_data Array of `BLOCKS_PER_BUCKET` blocks that will be stored in `bucket_store`
 */
extern void bucket_store_write_bucket_blocks_jazz(bucket_store *bucket_store, u64 bucket_id, const block bucket_data[BLOCKS_PER_BUCKET]);

void private_bucket_store_tests();
#endif // IS_TEST
#endif // CDS_PATH_ORAM_BUCKET_H
