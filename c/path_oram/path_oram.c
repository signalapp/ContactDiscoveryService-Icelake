// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "bucket.h"
#include "tree_path.h"
#include "position_map.h"
#include "stash.h"
#include "path_oram.h"
#include "util/util.h"

// struct oram layout must match Jasmin definitions in path_oram.jinc.
// Jasmin is the source of truth; validated at runtime by oram_validate_layout().
struct oram {
    bucket_store *bucket_store;
    position_map *position_map;
    stash *stash;
    tree_path *path;
    size_t capacity_blocks;
    oram_statistics statistics;
};

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void oram_validate_layout(void) {
    CHECK(offsetof(oram, bucket_store) == oram_bucket_store_offset_jazz());
    CHECK(offsetof(oram, position_map) == oram_position_map_offset_jazz());
    CHECK(offsetof(oram, stash) == oram_stash_offset_jazz());
    CHECK(offsetof(oram, path) == oram_path_offset_jazz());
    CHECK(offsetof(oram, capacity_blocks) == oram_capacity_blocks_offset_jazz());
    CHECK(offsetof(oram, statistics) == oram_statistics_offset_jazz());
    CHECK(sizeof(oram) == oram_sizeof_jazz());
}


oram* oram_create_120G_16shards(entropy_func getentropy) {
    oram_validate_layout();
    size_t stash_overflow_size = 100;
    size_t num_levels = 20;
    size_t num_blocks = (1ul << (num_levels-1)) * 1.625; // 851968

    oram *oram;
    CHECK(oram = calloc(1, oram_sizeof_jazz()));

    TEST_LOG("oram_create_120G_16shards: num_levels: %zu num_blocks: %zu", num_levels, num_blocks);

    // create a bucket store with 20 levels. This will hold ((size_t)1 << num_levels) - 1 = 1048575
    // buckets altogether, stored in an array of size 1048575 * ENCRYPTED_BUCKET_SIZE = 4,294,963,200B.
    // (Almost 4 GiB). The bucket_store itself requires 24 bytes.
    //
    // As this is for a 16 shard deployment, buckets will consume a total of about 64GiB.
    oram->bucket_store = bucket_store_create(num_levels);

    // Creates a stash that holds an array of blocks that can hold the "stash overflow" -
    // blocks that cannot be placed in the path - and temporarily holds all blocks in the current path.
    // for the parameters of this deployment this yields (60+20*BLOCKS_PER_BUCKET), or 120 blocks.
    // Since a single block is 1360B (1344B of data + 16B metadata), this is 163200B or 160KiB.
    //
    // It also stores 2 workspace arrays. One has 20 u64s to manage the occupancy of the buckets in the path,
    // the other has 120 u64s to manage the bucket assignments of the blocks in in the stash.
    oram->stash = stash_create(num_levels, stash_overflow_size);

    // Initialize the path to be from leaf 0 to the root. The path holds an 8B length and 20 u64 values.
    // The values are the bucket ids for the path from leaf 0 to the root.
    oram->path = tree_path_create(num_levels);

    oram->position_map = oram_position_map_create_120G_16shards(getentropy);

    oram->capacity_blocks = num_blocks;
    oram->statistics.recursion_depth = position_map_recursion_depth(oram->position_map);

    TEST_LOG("oram_create_120G_16shards: complete\n");
    return oram;
}


oram* oram_create_120G_16shards_posmap(entropy_func getentropy) {
    // top level position map must store locations of 851968 blocks, which fits
    // in 2536 blocks of 1344B each because we're using u32 entries. So we need
    // 4096 leaves in the bucket store, or 13 levels.
    size_t stash_overflow_size = 100;
    size_t num_levels = 13;
    size_t num_blocks = (1ul << num_levels); // 8192

    oram *oram;
    CHECK(oram = calloc(1, oram_sizeof_jazz()));

    // create a bucket store with 13 levels. This will hold ((size_t)1 << num_levels) - 1 = 8191
    // buckets altogether, stored in an array of size 8191 * ENCRYPTED_BUCKET_SIZE = 33,550,336B.
    // (32MiB). The bucket_store itself requires 24 bytes.
    //
    // As this is for a 16 shard deployment, buckets will consume a total of about 512MiB.
    oram->bucket_store = bucket_store_create(num_levels);

    // Creates a stash that holds an array of blocks that can hold the "stash overflow" -
    // blocks that cannot be placed in the path - and temporarily holds all blocks in the current
    // path. For the parameters of this deployment this yields (60+13*BLOCKS_PER_BUCKET), or 99 blocks.
    // Since a single block is 1360B (1344B of data + 16B metadata), this is 134640B or 131.5KiB.
    //
    // It also stores 2 workspace arrays. One has 13 u64s to manage the occupancy of the buckets in the path,
    // the other has 99 u64s to manage the bucket assignments of the blocks in in the stash.
    oram->stash = stash_create(num_levels, stash_overflow_size);

    // Initialize the path to be from leaf 0 to the root. The path holds an 8B length and 13 u64 values.
    // The values are the bucket ids for the path from leaf 0 to the root.
    oram->path = tree_path_create(num_levels);

    oram->position_map = scan_position_map_create_120G_16shards(getentropy);

    oram->capacity_blocks = num_blocks;
    oram->statistics.recursion_depth = position_map_recursion_depth(oram->position_map);

    TEST_LOG("oram_create_120G_16shards_posmap: complete\n");
    return oram;
}
oram* oram_create_depth16(entropy_func getentropy) {
    oram_validate_layout();
    size_t stash_overflow_size = 100;
    size_t num_levels = 16;
    size_t num_blocks = (1ul << (num_levels-1)) * 1.625; // 53248

    oram *oram;
    CHECK(oram = calloc(1, oram_sizeof_jazz()));

    // create a bucket store with 16 levels. This will hold ((size_t)1 << num_levels) - 1 = 65535
    // buckets altogether, stored in an array of size 65535 * ENCRYPTED_BUCKET_SIZE = 268,431,360B.
    // (About 256MiB). The bucket_store itself requires 24 bytes.
    //
    // As this is for a 16 shard deployment, buckets will consume a total of about 4GiB.
    oram->bucket_store = bucket_store_create(num_levels);

    // Creates a stash that holds an array of blocks that can hold the "stash overflow" -
    // blocks that cannot be placed in the path - and temporarily holds all blocks in the current path.
    // for the parameters of this deployment this yields (60+16*BLOCKS_PER_BUCKET), or 96 blocks.
    // Since a single block is 1360B (1344B of data + 16B metadata), this is 130560B or 127.5KiB.
    //
    // It also stores 2 workspace arrays. One has 16 u64s to manage the occupancy of the buckets in the path,
    // the other has 96 u64s to manage the bucket assignments of the blocks in the stash.
    oram->stash = stash_create(num_levels, stash_overflow_size);

    // Initialize the path to be from leaf 0 to the root. The path holds an 8B length and 16 u64 values.
    // The values are the bucket ids for the path from leaf 0 to the root.
    oram->path = tree_path_create(num_levels);

    oram->position_map = oram_position_map_create_depth16(getentropy);

    oram->capacity_blocks = num_blocks;
    oram->statistics.recursion_depth = position_map_recursion_depth(oram->position_map);

    return oram;
}

oram* oram_create_depth16_posmap(entropy_func getentropy) {
    // top level position map must store locations of 53248 blocks, which fits
    // in 159 blocks of 1344B each because we're using u32 entries. So we need
    // 256 leaves in the bucket store, or 9 levels.
    size_t stash_overflow_size = 100;
    size_t num_levels = 9;
    size_t num_blocks = 159; // blocks needed to hold 53248 block IDs (336 per block)

    oram *oram;
    CHECK(oram = calloc(1, oram_sizeof_jazz()));

    // create a bucket store with 9 levels. This will hold ((size_t)1 << num_levels) - 1 = 511
    // buckets altogether, stored in an array of size 511 * ENCRYPTED_BUCKET_SIZE = 2,093,056B.
    // (About 2MiB). The bucket_store itself requires 24 bytes.
    //
    // As this is for a 16 shard deployment, buckets will consume a total of about 32MiB.
    oram->bucket_store = bucket_store_create(num_levels);

    // Creates a stash that holds an array of blocks that can hold the "stash overflow" -
    // blocks that cannot be placed in the path - and temporarily holds all blocks in the current
    // path. For the parameters of this deployment this yields (60+9*BLOCKS_PER_BUCKET), or 87 blocks.
    // Since a single block is 1360B (1344B of data + 16B metadata), this is 118320B or 115.5KiB.
    //
    // It also stores 2 workspace arrays. One has 10 u64s to manage the occupancy of the buckets in the path,
    // the other has 87 u64s to manage the bucket assignments of the blocks in the stash.
    oram->stash = stash_create(num_levels, stash_overflow_size);

    // Initialize the path to be from leaf 0 to the root. The path holds an 8B length and 9 u64 values.
    // The values are the bucket ids for the path from leaf 0 to the root.
    oram->path = tree_path_create(num_levels);

    oram->position_map = scan_position_map_create_depth16(getentropy);

    oram->capacity_blocks = num_blocks;
    oram->statistics.recursion_depth = position_map_recursion_depth(oram->position_map);

    return oram;
}

void oram_destroy(oram *oram)
{
    // Acceptable if: this is not executed in an oram_access
    if (oram)
    {
        bucket_store_destroy(oram->bucket_store);
        position_map_destroy(oram->position_map);
        stash_destroy(oram->stash);
        tree_path_destroy(oram->path);
        free(oram);
    }
}

void oram_clear(oram *oram)
{
    bucket_store_clear(oram->bucket_store);
    stash_clear(oram->stash);
    oram->statistics = (oram_statistics){ .max_stash_overflow_count = 0 };
    // The position map has random placements so we do not need to clear these.
}

size_t oram_block_size(const oram *oram)
{
    (void)oram;  // Compile-time constant from bucket.h
    return BLOCK_DATA_SIZE_QWORDS;
}

size_t oram_capacity_blocks(const oram *oram)
{
    return oram->capacity_blocks;
}

void oram_collect_statistics(oram* oram) {
    double tenthousandth_root_one_half = 0.99993068768415357;
    size_t stash_sz = stash_num_overflow_blocks(oram->stash);
    oram_statistics* stats = &oram->statistics;
    ++stats->access_count;
    stats->stash_overflow_count = stash_sz;
    stats->sum_stash_overflow_count += stash_sz;
    stats->stash_overflow_ema10k = (1.0 - tenthousandth_root_one_half) * stash_sz + tenthousandth_root_one_half * stats->stash_overflow_ema10k;

    stats->max_stash_overflow_count = U64_TERNARY(stash_sz > stats->max_stash_overflow_count, stash_sz, stats->max_stash_overflow_count);
#ifdef IS_TEST
    if(stash_sz > stats->max_stash_overflow_count) {
        TEST_LOG("ORAM stash size increase: %zu", stash_sz);
    }
#endif // IS_TEST
}

const oram_statistics* oram_report_statistics(oram* oram) {
    const oram_statistics* pos_map_stats = position_map_oram_statistics(oram->position_map);
    oram_statistics* stats = &oram->statistics;
    // Acceptable if: not executed in an oram_access
    if(pos_map_stats) {
        stats->posmap_stash_overflow_count = pos_map_stats->stash_overflow_count;
        stats->posmap_max_stash_overflow_count = pos_map_stats->max_stash_overflow_count;
        stats->posmap_sum_stash_overflow_count = pos_map_stats->sum_stash_overflow_count;
        stats->posmap_stash_overflow_ema10k = pos_map_stats->stash_overflow_ema10k;
    } else {
        stats->posmap_stash_overflow_count = 0;
        stats->posmap_max_stash_overflow_count = 0;
        stats->posmap_sum_stash_overflow_count = 0;
        stats->posmap_stash_overflow_ema10k = 0;
    }
    return stats;
}

/**
 * @brief Jasmin function definitions for ORAM operations.
 */

extern error_t oram_put_jazz(oram *, u64 block_id, const u64 data[]);
extern error_t oram_position_map_put_jazz(oram *, u64 block_id, const u64 data[]);
extern error_t oram_function_access_get_jazz(oram* oram, u64 block_id, void* accessor_args);
extern error_t oram_function_access_put_jazz(oram* oram, u64 block_id, void* accessor_args);

error_t oram_put(oram* oram, u64 block_id, const u64 data[]) {
    error_t err = oram_put_jazz(oram, block_id, data);
    oram_collect_statistics(oram);
    return err;
}

error_t oram_position_map_put(oram* oram, u64 block_id, const u64 data[]) {
    error_t err = oram_position_map_put_jazz(oram, block_id, data);
    oram_collect_statistics(oram);
    return err;
}

error_t oram_function_access_get(oram* oram, u64 block_id, void* accessor_args) {
    error_t err = oram_function_access_get_jazz(oram, block_id, accessor_args);
    oram_collect_statistics(oram);
    return err;
}

error_t oram_function_access_put(oram* oram, u64 block_id, void* accessor_args) {
    error_t err = oram_function_access_put_jazz(oram, block_id, accessor_args);
    oram_collect_statistics(oram);
    return err;
}

#ifdef IS_TEST
#include <stdio.h>
#include <math.h>
#include <sys/random.h>
#include "util/util.h"
#include "util/tests.h"

extern error_t oram_get_jazz(oram *, u64 block_id, u64 buf[]);

error_t oram_get(oram* oram, u64 block_id, u64 buf[]) {
    error_t err = oram_get_jazz(oram, block_id, buf);
    oram_collect_statistics(oram);
    return err;
}

int init_oram_test()
{

    oram *oram = oram_create_depth16(getentropy);
    TEST_ASSERT(oram != NULL);

    size_t expected_num_blocks = 53248;
    size_t expected_num_levels = 16;

    TEST_ASSERT(oram->capacity_blocks == expected_num_blocks);
    TEST_ASSERT(oram->path->length == expected_num_levels);
    TEST_ASSERT(position_map_capacity(oram->position_map) == expected_num_blocks);

    oram_destroy(oram);
    return err_SUCCESS;
}

int getput_cycle_works()
{
    oram *oram = oram_create_depth16(getentropy);

    // create a buffer filled with "random" bits
    u64 buf[BLOCK_DATA_SIZE_QWORDS];
    for (u64 i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
    {
        buf[i] = 1 + i;
    }
    RETURN_IF_ERROR(oram_get(oram, 1000, buf));

    for (int i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
    {
        // oram_get of uninitialized block should overwrite buffer with UINT64_MAX
        TEST_ASSERT(buf[i] == UINT64_MAX);
    }

    // store some data in buf again
    for (int i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
    {
        buf[i] = 1 + i;
    }
    RETURN_IF_ERROR(oram_put(oram, 1000, buf));

    // finally retreive it and check equality
    memset(buf, 0, BLOCK_DATA_SIZE_QWORDS * sizeof(*buf));
    RETURN_IF_ERROR(oram_get(oram, 1000, buf));

    for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
    {
        // Make sure we read the correct data
        TEST_ASSERT(buf[i] == i + 1);
    }

    oram_destroy(oram);

    return err_SUCCESS;
}

int getput_must_put_to_change_block()
{
    oram *oram = oram_create_depth16(getentropy);
    u64 buf[BLOCK_DATA_SIZE_QWORDS];

    RETURN_IF_ERROR(oram_get(oram, 1000, buf));

    // change the data in our buffer
    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        buf[j] = j + 1;
    }

    // get it again
    RETURN_IF_ERROR(oram_get(oram, 1000, buf));

    // check that the block is still all zero
    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        TEST_ASSERT(buf[j] == UINT64_MAX);
    }
    oram_destroy(oram);

    return err_SUCCESS;
}

int test_oram_clears_stash() {
    oram *oram = oram_create_depth16(getentropy);
    u64 buf[BLOCK_DATA_SIZE_QWORDS];

    block b = {.id = 1000, .position = 1234};

    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        b.data[j] = j + 1;
    }
    RETURN_IF_ERROR(stash_add_block_jazz(oram->stash, &b));
    TEST_ASSERT(stash_num_overflow_blocks(oram->stash) == 1);

    RETURN_IF_ERROR(oram_get(oram, 1000, buf));
    // Now check that the data we got matches
    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        TEST_ASSERT(buf[j] == b.data[j]);
    }

    oram_clear(oram);

    TEST_ASSERT(stash_num_overflow_blocks(oram->stash) == 0); RETURN_IF_ERROR(oram_get(oram, 1000, buf));

    // Now check that the data we got is clear
    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        TEST_ASSERT(buf[j] == UINT64_MAX);
    }

    oram_destroy(oram);
    return err_SUCCESS;
}

static void initialization_test_group()
{
    RUN_TEST(init_oram_test());
}

static void getput_test_group()
{
    RUN_TEST(getput_cycle_works());
    RUN_TEST(getput_must_put_to_change_block());
    RUN_TEST(test_oram_clears_stash());
}

void run_path_oram_tests()
{
    initialization_test_group();
    getput_test_group();
}

#endif // IS_TEST
