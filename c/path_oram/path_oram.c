// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
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

struct oram
{
    bucket_store *bucket_store;
    position_map *position_map;
    stash *stash;
    size_t allocated_ub; // One above largest allocated block_id
    size_t capacity_blocks;

    size_t num_levels;
    tree_path *path;

    oram_statistics statistics;

    entropy_func getentropy;
};


static bool block_is_allocated(const oram *p_oram, u64 block_id)
{
    return block_id < p_oram->allocated_ub;
}

static oram* _create(size_t num_levels, size_t num_blocks, size_t stash_overflow_size, entropy_func getentropy) {
    // make sure the number of leaves in our bucket store isn't bigger than the number of blocks
    CHECK((1ul << (num_levels - 1)) <= num_blocks);

    oram *oram;
    CHECK(oram = calloc(1, sizeof(*oram)));
    oram->bucket_store = bucket_store_create(num_levels);
    
    oram->num_levels = bucket_store_num_levels(oram->bucket_store);
    oram->capacity_blocks = num_blocks; 

    oram->position_map = position_map_create(num_blocks, bucket_store_num_leaves(oram->bucket_store), stash_overflow_size, getentropy);
    oram->stash = stash_create(oram->num_levels, stash_overflow_size);
    oram->path = tree_path_create(0, bucket_store_root(oram->bucket_store));
    oram->getentropy = getentropy;


    oram->statistics.recursion_depth = position_map_recursion_depth(oram->position_map);
    TEST_LOG("create ORAM capacity_blocks: %zu bucket_store leaves: %zu", oram->capacity_blocks, bucket_store_num_leaves(oram->bucket_store));

    return oram;

}

oram* oram_create_for_available_mem(size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy) {
    size_t num_buckets = available_bytes / ENCRYPTED_BUCKET_SIZE;
    size_t num_levels = floor_log2(num_buckets + 1);
    size_t num_leaves = 1ul << (num_levels - 1);

    // We are assured that num_leaves < SIZE_MAX / ENCRYPTED_BUCKET_SIZE < SIZE_MAX / 4
    // When multiplying `num_leaves` by a floating point number between 1.0 and
    // 3.0 we will get a valid `size_t` greater than or equal to `num_leaves`.
    // We always want `num_leaves <= num_blocks` or we'll be wasting space. The upper
    // bound of 3.0 is arbitrary but is likely larger than we want and would cause
    // erratic stash behavior and degraded performance. 
    // In practice we expect values between 1.0 and 2.0.
    CHECK(load_factor >= 1.0 && load_factor <= 3.0); // Acceptable &&: not executed in an oram_access
    size_t num_blocks = num_leaves * load_factor;

    size_t actual_size = oram_size_bytes(num_levels, num_blocks, stash_overflow_size);
    while(actual_size > available_bytes && num_levels > 0) { // Acceptable &&: not executed in an oram_access
        TEST_LOG("decreasing num_levels actual_size: %zu requested: %zu levels: %zu", actual_size, available_bytes, num_levels);
        num_levels -= 1;
        num_leaves  = 1ul << (num_levels - 1);
        num_blocks = num_leaves * load_factor;
        actual_size = oram_size_bytes(num_levels, num_blocks, stash_overflow_size);
    }
    CHECK(num_levels > 0);
    CHECK(actual_size <= available_bytes);

    TEST_LOG("requested size: %zu actual size: %zu num_blocks: %zu num_levels: %zu", available_bytes, actual_size, num_blocks, num_levels);

    return _create(num_levels, num_blocks, stash_overflow_size, getentropy);
}

oram *oram_create(size_t capacity_u64, size_t stash_overflow_size, entropy_func getentropy)
{
    size_t num_blocks = (capacity_u64 / BLOCK_DATA_SIZE_QWORDS) + (capacity_u64 % BLOCK_DATA_SIZE_QWORDS == 0 ? 0 : 1);
    size_t num_levels = ceil_log2(num_blocks);

    return _create(num_levels, num_blocks, stash_overflow_size, getentropy);
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
    oram->allocated_ub = 0;
    oram->statistics = (oram_statistics){ .max_stash_overflow_count = 0 };
    // The position map has random placements so we do not need to clear these.
}

size_t oram_block_size(const oram *oram)
{
    return bucket_store_block_data_size(oram->bucket_store);
}

size_t oram_capacity_blocks(const oram *oram)
{
    return oram->capacity_blocks;
}

size_t oram_size_bytes(size_t num_levels, size_t num_blocks, size_t stash_overflow_size) {
    size_t num_leaves = (1ul << (num_levels - 1));
    size_t bucket_store_size = (2*num_leaves - 1)*ENCRYPTED_BUCKET_SIZE;
    size_t pos_map_size = position_map_size_bytes(num_blocks, stash_overflow_size);
    size_t stash_size = stash_size_bytes(num_levels, stash_overflow_size);
    size_t path_size = num_levels*sizeof(u64);

    return sizeof(oram) + bucket_store_size + pos_map_size + stash_size + path_size;
}

static u64 random_mod_by_pow_of_2(oram *oram, u64 modulus)
{
    uint64_t buf[1];
    oram->getentropy(buf, sizeof(buf));
    return buf[0] & (modulus - 1);
}

static void oram_collect_statistics(oram* oram) {
    double tenthousandth_root_one_half = 0.99993068768415357;
    size_t stash_size = stash_num_overflow_blocks(oram->stash);
    ++oram->statistics.access_count;
    oram->statistics.stash_overflow_count = stash_size;
    oram->statistics.sum_stash_overflow_count += stash_size;
    oram->statistics.stash_overflow_ema10k = (1.0 - tenthousandth_root_one_half) * stash_size + tenthousandth_root_one_half * oram->statistics.stash_overflow_ema10k;
    
    oram->statistics.max_stash_overflow_count = U64_TERNARY(stash_size > oram->statistics.max_stash_overflow_count, stash_size, oram->statistics.max_stash_overflow_count);
#ifdef IS_TEST
    if(stash_size > oram->statistics.max_stash_overflow_count) {
        TEST_LOG("ORAM stash size increase: %zu", stash_size);
    }
#endif // IS_TEST
}

/**
 * @brief read the path from the bucket store, performing the same sequence of instructions independent of the input.
 * Post-condition: the block with `id == target_block_id` will *not* be in the stash - neither the overflow or the path stash.
 * It will be in the block `*target` and the new position will be set.
 * 
 * @param oram 
 * @param path Path for block with ID `target_block_id`.
 * @param target_block_id ID of block to read
 * @param target On output, block with ID `target_block_id` will be available here
 * @param new_position Position for the target block after this access
 */
static void oram_read_path_for_block(oram* oram, const tree_path* path, u64 target_block_id, block *target, u64 new_position) {
    for(size_t i = 0; i < path->length; ++i) {
        stash_add_path_bucket(oram->stash, oram->bucket_store, path->values[i], target_block_id, target);
    }
    stash_scan_overflow_for_target(oram->stash, target_block_id, target);

    target->id = target_block_id;
    target->position = new_position;
}

/**
 * @brief Perform the requested access operation on a block's data.
 * 
 * @param target 
 * @param accessor function that performs the accesses
 * @param accessor_args input/output arguments for the accessor function
 */
static error_t perform_access_op(
    block* target, 
    accessor_func accessor,
    void* accessor_args) 
{
    CHECK(accessor != NULL);
    RETURN_IF_ERROR(accessor(target->data, accessor_args));

    return err_SUCCESS;
}

static error_t oram_access(
    oram *oram,
    u64 block_id,
    accessor_func accessor,
    void* accessor_args)
{
    block target_block = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};
    memset(target_block.data, 255, BLOCK_DATA_SIZE_BYTES);
    size_t max_position = bucket_store_num_leaves(oram->bucket_store);

    u64 new_position = random_mod_by_pow_of_2(oram, max_position);
    u64 x = 0;
    RETURN_IF_ERROR(position_map_read_then_set(oram->position_map, block_id, new_position, &x));
    // bucket locations are always even
    x *= 2;

    tree_path_update(oram->path, x);
    tree_path* path = oram->path;

    oram_read_path_for_block(oram, path, block_id, &target_block, new_position * 2);
    RETURN_IF_ERROR(perform_access_op(&target_block, accessor, accessor_args));

    RETURN_IF_ERROR(stash_add_block(oram->stash, &target_block));

    stash_build_path(oram->stash, oram->path);

    for (size_t i = 0; i < path->length; ++i)
    {
        u64 bucket_id = path->values[i];
        bucket_store_write_bucket_blocks(oram->bucket_store, bucket_id, stash_path_blocks(oram->stash) + i * BLOCKS_PER_BUCKET);
    }
    oram_collect_statistics(oram);
    return err_SUCCESS;
}

error_t oram_function_access(oram* oram, u64 block_id, accessor_func accessor, void* accessor_args) {
    // Acceptable if: failure is a bug that leaks more than the timing here
    if (block_is_allocated(oram, block_id))
    {
        return oram_access(oram, block_id, accessor, accessor_args);
    }
    return err_ORAM__ACCESS_UNALLOCATED_BLOCK;
}

typedef struct {
    size_t in_data_start;
    size_t in_data_len;
    const u64 *in_data;
    u64* out_data;
} write_accessor_args;

static error_t write_accessor(u64* block_data, void* vargs) {
    write_accessor_args* args = vargs;
    CHECK(args->in_data != 0);
    for(size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i) {
        bool cond = (i >= args->in_data_start) & (i < args->in_data_start + args->in_data_len);

        // We can only access the source data in the range [args->in_data, args->in_data + args->in_data_len) without
        // risking a SIGSEGV. We need to provide a valid index for every conditional read that will also
        // be the correct source index when `cond` is true. The choice of source index here is (1) always in
        // the needed range and (2) is 0 when i == args->ind_data_start and cond becomes true. This means we don't
        // have a memory violation and we do copy the correct data.
        size_t source_index = (i + args->in_data_len -  (args->in_data_start % args->in_data_len)) % args->in_data_len;
        
        // Acceptable if: we allow leakage of request type
        if(args->out_data) {
            cond_obv_cpy_u64(cond, args->out_data + source_index, block_data + i);
        }
        cond_obv_cpy_u64(cond, block_data + i, args->in_data + source_index);
    }
    return err_SUCCESS;
}

error_t oram_put(oram *oram, u64 block_id, const u64 data[])
{
    // Acceptable if: failure is a bug that leaks more than the timing here
    if (block_is_allocated(oram, block_id))
    {
        write_accessor_args args = { .in_data_start = 0, .in_data_len = BLOCK_DATA_SIZE_QWORDS, .in_data = data, .out_data = NULL };
        return oram_access(oram, block_id, write_accessor, &args);
    }
    return err_ORAM__ACCESS_UNALLOCATED_BLOCK;
}

error_t oram_put_partial(oram *oram, u64 block_id, size_t start, size_t len, u64 data[len], u64 prev_data[len])
{
    // Acceptable if: failure is a bug that leaks more than the timing here
    if (block_is_allocated(oram, block_id))
    {
        CHECK(start + len <= BLOCK_DATA_SIZE_QWORDS);
        write_accessor_args args = { .in_data_start = start, .in_data_len = len, .in_data = data, .out_data = prev_data };
        return oram_access(oram, block_id, write_accessor, &args);
    }
    return err_ORAM__ACCESS_UNALLOCATED_BLOCK;
}

u64 oram_allocate_block(oram *oram)
{
    // Acceptable if: not executed in an oram_access
    if (oram->allocated_ub < oram->capacity_blocks)
    {
        return (oram->allocated_ub)++;
    }
    return UINT64_MAX;
}
u64 oram_allocate_contiguous(oram *oram, size_t num_blocks)
{
    register size_t requested_ub = oram->allocated_ub + num_blocks;
    // Acceptable if: not executed in an oram_access
    if (requested_ub <= oram->capacity_blocks)
    {
        register size_t next_block = oram->allocated_ub;
        oram->allocated_ub = requested_ub;
        return next_block;
    }
    return UINT64_MAX;
}

const oram_statistics* oram_report_statistics(oram* oram) {
    const oram_statistics* pos_map_stats = position_map_oram_statistics(oram->position_map);
    // Acceptable if: not executed in an oram_access
    if(pos_map_stats) {
        oram->statistics.posmap_stash_overflow_count = pos_map_stats->stash_overflow_count;
        oram->statistics.posmap_max_stash_overflow_count = pos_map_stats->max_stash_overflow_count;
        oram->statistics.posmap_sum_stash_overflow_count = pos_map_stats->sum_stash_overflow_count;
        oram->statistics.posmap_stash_overflow_ema10k = pos_map_stats->stash_overflow_ema10k;
    } else {
        oram->statistics.posmap_stash_overflow_count = 0;
        oram->statistics.posmap_max_stash_overflow_count = 0;
        oram->statistics.posmap_sum_stash_overflow_count = 0;
        oram->statistics.posmap_stash_overflow_ema10k = 0;
    }
    return &(oram->statistics);
}

#ifdef IS_TEST
#include <stdio.h>
#include <math.h>
#include <sys/random.h>
#include "util/util.h"
#include "util/tests.h"


typedef struct {
    u64* out_data;
    size_t out_data_start;
    size_t out_data_len;
} read_accessor_args;

static error_t read_accessor(u64* block_data, void* vargs) {
    read_accessor_args* args = vargs;
    CHECK(args->out_data != NULL);
    for(size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i) {
        bool cond = (i >= args->out_data_start) & (i < args->out_data_start + args->out_data_len);

        // We can only access the destination data in the range [args->out_data, args->out_data + args->out_data_len) without
        // risking a SIGSEGV. We need to provide a valid index for every conditional read that will also
        // be the correct source index when `cond` is true. The choice of source index here is (1) always in
        // the needed range and (2) is 0 when i == args->out_data_start and cond becomes true. This means we don't
        // have a memory violation and we do copy the correct data.
        size_t source_index = (i + args->out_data_len -  (args->out_data_start % args->out_data_len)) % args->out_data_len;
        
        // Acceptable if: we allow leakage of request type
        cond_obv_cpy_u64(cond, args->out_data + source_index, block_data + i);
    }
    return err_SUCCESS;
}


error_t oram_get(oram *oram, u64 block_id, u64 buf[])
{
    // Acceptable if: failure is a bug that leaks more than the timing here
    if (block_is_allocated(oram, block_id))
    {
        read_accessor_args args = {.out_data = buf, .out_data_start = 0, .out_data_len = BLOCK_DATA_SIZE_QWORDS};
        return oram_access(oram, block_id, read_accessor, &args);
    }
    return err_ORAM__ACCESS_UNALLOCATED_BLOCK;
}


error_t oram_get_partial(oram *oram, u64 block_id, size_t start, size_t len, u64 data[len])
{
    // Acceptable if: failure is a bug that leaks more than the timing here
    if (block_is_allocated(oram, block_id))
    {
        read_accessor_args args = {.out_data = data, .out_data_start = start, .out_data_len = len};
        return oram_access(oram, block_id, read_accessor, &args);
    }
    return err_ORAM__ACCESS_UNALLOCATED_BLOCK;
}

void print_oram(const oram *oram)
{
    printf("ORAM state: bucket cap (B): %zu position_map_cap (entries): %zu\n",
           bucket_store_capacity_bytes(oram->bucket_store),
           position_map_capacity(oram->position_map));
}

static size_t expected_num_blocks_for_capacity(size_t capacity) {
    return (capacity / BLOCK_DATA_SIZE_QWORDS) + (capacity % BLOCK_DATA_SIZE_QWORDS == 0 ? 0 : 1);
}

int test_ceil_log()
{

    TEST_ASSERT(ceil_log2(1) == 0);
    TEST_ASSERT(ceil_log2(2) == 1);
    TEST_ASSERT(ceil_log2(3) == 2);
    TEST_ASSERT(ceil_log2(4) == 2);
    TEST_ASSERT(ceil_log2(5) == 3);
    TEST_ASSERT(ceil_log2(6) == 3);
    TEST_ASSERT(ceil_log2(7) == 3);
    TEST_ASSERT(ceil_log2(8) == 3);
    TEST_ASSERT(ceil_log2(9) == 4);

    TEST_ASSERT(ceil_log2(1ULL << 33) == 33);
    TEST_ASSERT(ceil_log2((1ULL << 33) + 1) == 34);

    return 0;
}

int init_oram_test()
{
    size_t capacity = 1 << 20;

    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    TEST_ASSERT(oram != NULL);

    size_t expected_block_size = BLOCK_DATA_SIZE_QWORDS;
    size_t expected_num_blocks = expected_num_blocks_for_capacity(capacity);
    size_t store_capacity = bucket_store_capacity_bytes(oram->bucket_store) / 8;

    TEST_ASSERT(!(store_capacity < (capacity/2) || store_capacity > capacity));
    TEST_ASSERT(bucket_store_block_data_size(oram->bucket_store) == expected_block_size);
    TEST_ASSERT(position_map_capacity(oram->position_map) == expected_num_blocks);

    oram_destroy(oram);
    return err_SUCCESS;
}

int init_odd_capacity_test()
{
    size_t capacity = (1 << 20) + 1331;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    TEST_ASSERT(oram != NULL);

    size_t expected_block_size = BLOCK_DATA_SIZE_QWORDS;
    size_t expected_num_blocks = expected_num_blocks_for_capacity(capacity);
    size_t store_capacity = bucket_store_capacity_bytes(oram->bucket_store) / 8;

    TEST_ASSERT(!(store_capacity < (capacity/2) || store_capacity > capacity));
    TEST_ASSERT(bucket_store_block_data_size(oram->bucket_store) == expected_block_size);
    TEST_ASSERT(position_map_capacity(oram->position_map) == expected_num_blocks);

    oram_destroy(oram);
    return err_SUCCESS;
}

int allocate_until_full_test()
{
    size_t capacity = 1 << 16;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    size_t expected_num_blocks = expected_num_blocks_for_capacity(capacity);
    for (size_t i = 0; i < expected_num_blocks; ++i)
    {
        u64 block_id = oram_allocate_block(oram);
        TEST_ASSERT(block_id == i);
    }

    // it should be full now, so lets make sure it doesn't give a new ID
    u64 block_id = oram_allocate_block(oram);
    TEST_ASSERT(block_id == UINT64_MAX);

    u64 buf[BLOCK_DATA_SIZE_QWORDS];
    RETURN_IF_ERROR(oram_get(oram, 21, buf));

    oram_destroy(oram);

    return err_SUCCESS;
}

int allocate_contiguous_until_full_test()
{
    size_t capacity = 1 << 20;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    size_t expected_num_blocks = expected_num_blocks_for_capacity(capacity);
    size_t blocks_per_allocation = 1 << 4;
    for (size_t i = 0; i < expected_num_blocks / blocks_per_allocation; ++i)
    {
        u64 block_id = oram_allocate_contiguous(oram, blocks_per_allocation);
        TEST_ASSERT(block_id == i * blocks_per_allocation);
    }

    // it should be full now, so lets make sure it doesn't give a new ID
    u64 block_id = oram_allocate_contiguous(oram, 1 + expected_num_blocks % blocks_per_allocation);
    TEST_ASSERT(block_id == UINT64_MAX);

    oram_destroy(oram);

    return err_SUCCESS;
}

int getput_cycle_works()
{
    size_t capacity = 1 << 23;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);

    oram_allocate_contiguous(oram, 1330);
    oram_allocate_block(oram);


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

int getput_only_accesses_allocated_blocks()
{

    size_t capacity = 1 << 20;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    u64 buf[BLOCK_DATA_SIZE_QWORDS];

    // Allocate some blocks
    oram_allocate_contiguous(oram, 1330);
    size_t eleven_cubed = oram_allocate_block(oram);

    // Test that we can get and put all of these blocks
    for (size_t i = 0; i <= eleven_cubed; ++i)
    {
        RETURN_IF_ERROR(oram_get(oram, i, buf));
        RETURN_IF_ERROR(oram_put(oram, i, buf));
    }

    // Now make sure we cannot get or put unallocated blocks
    TEST_ASSERT(err_ORAM__ACCESS_UNALLOCATED_BLOCK == oram_get(oram, eleven_cubed + 1, buf));
    TEST_ASSERT(err_ORAM__ACCESS_UNALLOCATED_BLOCK == oram_put(oram, eleven_cubed + 1, buf));

    oram_destroy(oram);

    return err_SUCCESS;
}

int getput_correctly_accesses_allocated_blocks()
{

    size_t capacity = 1 << 20;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    u64 buf[BLOCK_DATA_SIZE_BYTES];

    for (size_t j = 0; j < BLOCK_DATA_SIZE_BYTES; ++j)
    {
        buf[j] = j + 1;
    }

    // Allocate some blocks
    oram_allocate_contiguous(oram, 1330);
    size_t eleven_cubed = oram_allocate_block(oram);

    // Test that we can get and put all of these blocks
    for (size_t i = 0; i <= eleven_cubed; ++i)
    {

        RETURN_IF_ERROR(oram_put(oram, i, buf));
        RETURN_IF_ERROR(oram_get(oram, i, buf));

        for (size_t j = 0; j < BLOCK_DATA_SIZE_BYTES; ++j)
        {
            TEST_ASSERT(buf[j] == j + 1);
        }
    }

    oram_destroy(oram);

    return err_SUCCESS;
}

int getput_must_put_to_change_block()
{
    size_t capacity = 1 << 20;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    u64 buf[BLOCK_DATA_SIZE_QWORDS];

    // Allocate some blocks
    oram_allocate_contiguous(oram, 1330);
    oram_allocate_block(oram);


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

int test_oram_clears_stash() {size_t capacity = 1 << 20;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);
    u64 buf[BLOCK_DATA_SIZE_QWORDS];

    // Allocate some blocks
    oram_allocate_contiguous(oram, 1330);
    oram_allocate_block(oram);

    block b = {.id = 1000, .position = 1234};

    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        b.data[j] = j + 1;
    }
    RETURN_IF_ERROR(stash_add_block(oram->stash, &b));
    TEST_ASSERT(stash_num_overflow_blocks(oram->stash) == 1);

    RETURN_IF_ERROR(oram_get(oram, 1000, buf));
    // Now check that the data we got matches
    for (size_t j = 0; j < BLOCK_DATA_SIZE_QWORDS; ++j)
    {
        TEST_ASSERT(buf[j] == b.data[j]);
    }

    oram_clear(oram);

    // reallocate the blocks so we can get it without error
    oram_allocate_contiguous(oram, 1330);
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
    RUN_TEST(test_ceil_log());
    RUN_TEST(init_oram_test());
    RUN_TEST(init_odd_capacity_test());
}

static void allocate_test_group()
{
    RUN_TEST(allocate_until_full_test());
    RUN_TEST(allocate_contiguous_until_full_test());
}

static void getput_test_group()
{
    RUN_TEST(getput_cycle_works());
    RUN_TEST(getput_only_accesses_allocated_blocks());
    RUN_TEST(getput_correctly_accesses_allocated_blocks());
    RUN_TEST(getput_must_put_to_change_block());
    RUN_TEST(test_oram_clears_stash());
}

void run_path_oram_tests()
{
    initialization_test_group();
    allocate_test_group();
    getput_test_group();
}

#endif // IS_TEST
