// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "stash.h"
#include "bucket.h"
#include "tree_path.h"
#include "util/log.h"

// struct stash layout must match Jasmin definitions in stash.jinc.
// Jasmin is the source of truth; validated at runtime by stash_validate_layout().
struct stash
{
    /**
     * @brief Array of all blocks stored in the stash
     */
    block* blocks;
    /**
     * @brief Convenience pointer to the part of `blocks` that contains blocks for the current path
     */
    block* path_blocks;
    /**
     * @brief Convenience pointer to the part of `blocks` that contains blocks for the overflow that 
     * either does not fit or has not been placed in the current stash.
     */
    block* overflow_blocks;
    /**
     * @brief Total number of blocks that can be held in the `blocks` array.
     */
    size_t num_blocks;
    /**
     * @brief Number of blocks in the stash that can be used to store overflow blocks that can't be allocated to the 
     * `path_blocks`. This is the upper bound for the `overflow` array.
     * 
     */
    size_t overflow_capacity;

    // scratch space for block placement computations
    u64* bucket_occupancy;
    u64* bucket_assignments;
};

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void stash_validate_layout(void) {
    CHECK(offsetof(stash, blocks) == stash_blocks_offset_jazz());
    CHECK(offsetof(stash, path_blocks) == stash_path_blocks_offset_jazz());
    CHECK(offsetof(stash, overflow_blocks) == stash_overflow_blocks_offset_jazz());
    CHECK(offsetof(stash, num_blocks) == stash_num_blocks_offset_jazz());
    CHECK(offsetof(stash, overflow_capacity) == stash_overflow_capacity_offset_jazz());
    CHECK(offsetof(stash, bucket_occupancy) == stash_bucket_occupancy_offset_jazz());
    CHECK(offsetof(stash, bucket_assignments) == stash_bucket_assignments_offset_jazz());
    CHECK(sizeof(stash) == stash_sizeof_jazz());
}

typedef enum {
    block_type_overflow,
    block_type_path
} block_type;

stash *stash_create(size_t path_length, size_t overflow_size)
{
    size_t num_path_blocks = BLOCKS_PER_BUCKET * path_length;
    size_t num_blocks = overflow_size + num_path_blocks;
    stash *result;
    CHECK(result = calloc(1, sizeof(*result)));
    CHECK(result->blocks = calloc(num_blocks, sizeof(*result->blocks)));
    result->path_blocks = result->blocks;
    result->overflow_blocks = result->blocks + num_path_blocks;
    result->num_blocks = num_blocks;
    result->overflow_capacity = num_blocks - num_path_blocks; 
    CHECK(overflow_size == result->overflow_capacity);

    CHECK(result->bucket_occupancy = calloc(path_length, sizeof(*result->bucket_occupancy)));
    CHECK(result->bucket_assignments = calloc(num_blocks, sizeof(*result->bucket_assignments)));

    memset(result->blocks, 255,  sizeof(result->blocks[0]) * num_blocks);
    return result;
}

void stash_destroy(stash *stash)
{
    if (stash)
    {
        free(stash->blocks);
        free(stash->bucket_occupancy);
        free(stash->bucket_assignments);
    }
    free(stash);
}

const block* stash_path_blocks(const stash* stash) {
    return stash->path_blocks;
}

size_t stash_num_overflow_blocks(const stash* stash) {
    size_t result = 0;
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        result += U64_TERNARY(stash->overflow_blocks[i].id != EMPTY_BLOCK_ID, 1, 0);
    }
    return result;
}

error_t stash_clear(stash* stash) {
    memset(stash->blocks, 255,  sizeof(stash->blocks[0]) * stash->num_blocks);
    return err_SUCCESS;
}

#ifdef IS_TEST
#include <stdio.h>
#include <sys/random.h>
#include "util/tests.h"

void stash_print(const stash *stash)
{
    size_t num_blocks = 0;
    for (size_t i = 0; i < stash->overflow_capacity; ++i)
    {
        if (stash->overflow_blocks[i].id != EMPTY_BLOCK_ID)
        {
            num_blocks++;
        }
    }
    printf("Stash holds %zu blocks.\n", num_blocks);
    for (size_t i = 0; i < stash->overflow_capacity; ++i)
    {
        if (stash->overflow_blocks[i].id != EMPTY_BLOCK_ID)
        {
            printf("block_id: %" PRIu64 "\n", stash->overflow_blocks[i].id);
        }
    }
}


int test_cond_cpy_block() {
    block b1 = {.id = 1, .position = 1023, .data = {9,8,7,6,5,4,3,2,1}};
    block b1_orig = {.id = 1, .position = 1023, .data = {9,8,7,6,5,4,3,2,1}};
    block b2 = {.id = 2, .position = 2047, .data = {19,18,17,16,15,14,13,12,11}};
    block b2_orig = {.id = 2, .position = 2047, .data = {19,18,17,16,15,14,13,12,11}};
    block target = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};
    block mt_block = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};


    cond_copy_block_jazz(false, &target, &b1);
    TEST_ASSERT(memcmp(&mt_block, &target, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b1_orig, &b1, sizeof(block)) == 0);

    cond_copy_block_jazz(true, &target, &b1);
    TEST_ASSERT(memcmp(&target, &b1, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b1_orig, &b1, sizeof(block)) == 0);

    cond_swap_blocks_jazz(false, &b1, &b2);
    TEST_ASSERT(memcmp(&b1_orig, &b1, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b2_orig, &b2, sizeof(block)) == 0);

    cond_swap_blocks_jazz(true, &b1, &b2);
    TEST_ASSERT(memcmp(&b1_orig, &b2, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b2_orig, &b1, sizeof(block)) == 0);

    return 0;
}

int test_oblv_sort() {
    size_t num_blocks = 30;
    block blocks[30] = {
        {.id = 1},
        {.id = 2},
        {.id = 3},
        {.id = 4},
        {.id = 5},
        {.id = 6},
        {.id = 7},
        {.id = 8},
        {.id = 9},
        {.id = 10},
        {.id = 11},
        {.id = 12},
        {.id = 13},
        {.id = 14},
        {.id = 15},
        {.id = 16},
        {.id = 17},
        {.id = 18},
        {.id = 19},
        {.id = 20},
    };

    u64 bucket_assignments[30] = {
        0,7,UINT64_MAX,2,9,UINT64_MAX,4,11,UINT64_MAX,6,1,UINT64_MAX,8,3,UINT64_MAX,10,5,0,5,0
    };

    block original_blocks[30];
    u64 original_bucket_assignments[30];
    memcpy(original_blocks, blocks, sizeof(blocks));
    memcpy(original_bucket_assignments, bucket_assignments, sizeof(bucket_assignments));

    odd_even_msort_jazz(blocks, bucket_assignments, 0, num_blocks);
    for(size_t i = 1; i < num_blocks; ++i) {
        // check that it is sorted
        TEST_ASSERT(bucket_assignments[i-1] <= bucket_assignments[i]);
        // and that the blocks and bucket assignments moved together
        bool found = false;
        for(size_t j = 0; j < num_blocks; ++j) {
            if(blocks[i].id == original_blocks[j].id) {
                found = true;
                TEST_ASSERT(bucket_assignments[i] == original_bucket_assignments[j]);
                break;
            }
        }
        TEST_ASSERT(found);

    }
    return err_SUCCESS;
}


static void fill_block_data(u64 data[static BLOCK_DATA_SIZE_QWORDS])
{
    for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
    {
        data[i] = rand();
    }
}

static int check_stash_entry(block e, u64 expected_block_id, const u64 expected_data[BLOCK_DATA_SIZE_QWORDS], u64 expected_position)
{
    TEST_ASSERT(e.id == expected_block_id);
    TEST_ASSERT(e.position == expected_position);
    if (expected_block_id != EMPTY_BLOCK_ID)
    {
        for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
        {
            TEST_ASSERT(e.data[i] == expected_data[i]);
        }
    }
    return 0;
}


/*
Return the bucket ID for a leaf node that has a given bucket_id on its path, uniform random.
*/
u64 random_position_for_bucket(u64 bucket_id) {
    u64 lb = tree_path_lower_bound_jazz(bucket_id);
    u64 ub = tree_path_upper_bound_jazz(bucket_id);
    u64 rnd = 0;
    getentropy(&rnd, sizeof(rnd));
    return lb + (rnd % (ub+1 - lb)); // need the +1 because it is an inclusive upper bound
}
static void generate_blocks_for_bucket(u64 bucket_id, u64 block_id_start, size_t num_nonempty_blocks, block blocks[3]) {
    block empty = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};
    u64 num_created = 0;
    u64 rnd[] = {0};
    getentropy(rnd, sizeof(rnd));

    // This code is dependent of BLOCKS_PER_BUCKET == 3 - To generalize could use a random shuffle
    // If we have either 1 or 2 non-empty blocks, one of our three blocks in our bucket will be
    // special: for 1 non-empty block the special one is the non-empty one. For 2 non-empty blocks,
    // the special one is the empty one. 
    //
    // Handling 0 or 3 non-empty blocks is easy.
    size_t special_block_idx = rnd[0] % BLOCKS_PER_BUCKET;
    for(size_t  i = 0; i < BLOCKS_PER_BUCKET; ++i) {
        bool fill_block = (num_nonempty_blocks == BLOCKS_PER_BUCKET)
                            || (num_nonempty_blocks == 2 && i != special_block_idx)
                            || (num_nonempty_blocks == 1 && i == special_block_idx);
        
        block block = {.id = block_id_start + num_created, .position = random_position_for_bucket(bucket_id)};
        blocks[i] = fill_block ? block : empty;
        num_created += fill_block ? 1 : 0;
    }

}

static void load_bucket_store(bucket_store* bucket_store, size_t num_levels, const tree_path* path, bucket_density density, size_t* num_blocks_created) {
    u8 bucket_data[DECRYPTED_BUCKET_SIZE];
    block* bucket_blocks = (block*) bucket_data;
    *num_blocks_created = 0;
    for(size_t level = 0; level < path->length; ++level) {
        size_t num_blocks = 0;
        switch(density) {
        case bucket_density_empty:
            num_blocks = 0;
            break;
        case bucket_density_sparse:
            num_blocks = rand() % 2;
            break;
        case bucket_density_dense:
            num_blocks = 1 + rand() % BLOCKS_PER_BUCKET;
            break;
        case bucket_density_full:
            num_blocks = BLOCKS_PER_BUCKET;
        }
        *num_blocks_created += num_blocks;
        generate_blocks_for_bucket(path->values[level], *num_blocks_created, num_blocks, bucket_blocks);
        bucket_store_write_bucket_blocks_jazz(bucket_store, path->values[level], bucket_blocks);
    }
    
}

int test_load_bucket_path_to_stash(bucket_density density) {
    size_t num_levels = 18;
    stash *stash = stash_create(num_levels, TEST_STASH_SIZE);
    bucket_store* bucket_store = bucket_store_create(num_levels);
    u64 leaf = 157142;
    tree_path* path = tree_path_create(num_levels);
    tree_path_update_jazz(path, leaf);
    size_t num_blocks_added = 0;
    load_bucket_store(bucket_store, num_levels, path, density, &num_blocks_added);

    u64 target_block_id = num_blocks_added / 2;
    block target = {.id=EMPTY_BLOCK_ID};
    for(size_t i = 0; i < path->length; ++i) {
        stash_add_path_bucket_jazz(stash, bucket_store, path->values[i], target_block_id, &target);
    }

    TEST_ASSERT(target.id == target_block_id);
    for(size_t i = 0; i < path->length; ++i) {
        block* bucket_blocks = stash->path_blocks + i * BLOCKS_PER_BUCKET;
        for(size_t b = 0; b < BLOCKS_PER_BUCKET; ++b) {
            TEST_ASSERT(bucket_blocks[b].id != target_block_id);
        }
    }

    stash_destroy(stash);
    bucket_store_destroy(bucket_store);
    tree_path_destroy(path);
    return 0;
}


int test_stash_insert_read()
{
    stash *stash = stash_create(20, TEST_STASH_SIZE);
    u64 data0[BLOCK_DATA_SIZE_QWORDS];
    u64 data1[BLOCK_DATA_SIZE_QWORDS];
    u64 block_id0 = 1234;
    u64 block_id1 = 45678;
    fill_block_data(data0);
    fill_block_data(data1);
    block b0 = {.id = block_id0, .position = 2};
    block b1 = {.id = block_id1, .position = 3};
    block target0 = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};
    block target1 = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};
    memcpy(b0.data, data0, sizeof(data0));
    memcpy(b1.data, data1, sizeof(data1));

    RETURN_IF_ERROR(stash_add_block_jazz(stash, &b0));
    RETURN_IF_ERROR(stash_add_block_jazz(stash, &b1));
    bool b0_in_stash = false;
    bool b1_in_stash = false;
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        b0_in_stash = b0_in_stash || (stash->overflow_blocks[i].id == b0.id);
        b1_in_stash = b1_in_stash || (stash->overflow_blocks[i].id == b1.id);
    }
    TEST_ASSERT(b0_in_stash);
    TEST_ASSERT(b1_in_stash);

    stash_scan_overflow_for_target_jazz(stash, block_id0, &target0);
    stash_scan_overflow_for_target_jazz(stash, block_id1, &target1);

    check_stash_entry(target0, block_id0, data0, 2);
    check_stash_entry(target1, block_id1, data1, 3);

    // check that it is no longer in stash
    b0_in_stash = false;
    b1_in_stash = false;
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        b0_in_stash = b0_in_stash || (stash->overflow_blocks[i].id == b0.id);
        b1_in_stash = b1_in_stash || (stash->overflow_blocks[i].id == b1.id);
    }
    TEST_ASSERT(!b0_in_stash);
    TEST_ASSERT(!b1_in_stash);

    stash_destroy(stash);
    return err_SUCCESS;
}

int test_fill_stash() {
    size_t small_stash_size = 20;
    stash *stash = stash_create(20, small_stash_size);
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        block b = {.id = i, .position = 2*(100+i)};
        RETURN_IF_ERROR(stash_add_block_jazz(stash, &b));
        // check that it is in the stash
    }

    block b = {.id = stash->overflow_capacity, .position = 2*(100+stash->overflow_capacity)};

    // This will trigger an extension of the stash
    error_t err = stash_add_block_jazz(stash, &b);

    TEST_ASSERT(err == err_ORAM__STASH_EXHAUSTION);

    stash_destroy(stash);
    return 0;
}

#endif // IS_TEST
