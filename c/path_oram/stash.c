// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "stash.h"
#include "bucket.h"
#include "tree_path.h"
#include "util/log.h"

// We grow additively instead of doubling because the stash size process has a
// significant negative drift that increases as the size of the stash increases.
// One effect is that if the stash size is, e.g., 50, then it is very likely
// to go all the way down to zero before it goes to 60. Growing by 20 we have a
// vanishingly small probability of growing again before we clear the entire stash
// and essentially start over.
#define STASH_GROWTH_INCREMENT 20

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
     * @brief Length of the paths for the ORAM using this stash.
     */
    size_t path_length;
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


typedef enum {
    block_type_overflow,
    block_type_path
} block_type;

size_t stash_size_bytes(size_t path_length, size_t overflow_size) {
    size_t num_path_blocks = BLOCKS_PER_BUCKET * path_length;
    size_t num_blocks = overflow_size + num_path_blocks;
    
    // stash struct + blocks + bucket_occupancy + bucket_assignments
    return sizeof(stash) + num_blocks*sizeof(block) + path_length*sizeof(u64) + num_blocks*sizeof(u64);
}

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
    result->path_length = path_length;

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

static void stash_extend_overflow(stash* stash) {
    TEST_LOG("extending overflow from %zu to %zu", stash->overflow_capacity, stash->overflow_capacity + STASH_GROWTH_INCREMENT);
    size_t new_num_blocks = stash->num_blocks + STASH_GROWTH_INCREMENT;

    // (re)allocate new space, free the old
    CHECK(stash->blocks = realloc(stash->blocks, new_num_blocks * sizeof(*stash->blocks)));
    free(stash->bucket_assignments);
    CHECK(stash->bucket_assignments = calloc(new_num_blocks, sizeof(*stash->bucket_assignments)));

    // update our alias pointers
    stash->path_blocks = stash->blocks;
    stash->overflow_blocks = stash->blocks + stash->path_length * BLOCKS_PER_BUCKET;

    // initialize new memory
    memset(stash->blocks + stash->num_blocks, 255,  sizeof(stash->blocks[0]) * STASH_GROWTH_INCREMENT);

    // update counts
    stash->num_blocks += STASH_GROWTH_INCREMENT;
    stash->overflow_capacity += STASH_GROWTH_INCREMENT;
}

/** returns the index of the last nonempty blocks in overflow */
static size_t stash_overflow_ub(const stash* stash) {
    // setting this value to be true will allow stash maintenance operations to
    // stop at a known upper bound for used stash entries. In practice this provides ~20%
    // increase in throughput.
    //
    // When this value is `true`, the length of the computation and the pattern of memory accesses will depend 
    // on the current number of items in the overflow stash and hence may allow an attacker to infer some 
    // information about the request flow such as likelihood that there were multiple repeated E164s requested
    // in a short window. If repeated accesses are well spaced, this will contain negligible information.
    //
    // We will want to know the current and maximum overflow size for health monitoring, and if we report that, 
    // there is no value in obfuscating it in the computation.
    bool allow_overflow_size_leak = true;

    size_t i = stash->overflow_capacity;
    if(allow_overflow_size_leak) {
        while( i > 0) {
            if(stash->overflow_blocks[i-1].id != EMPTY_BLOCK_ID) break;
            --i;
        }
    }
    return i;
}

size_t stash_num_overflow_blocks(const stash* stash) {
    size_t result = 0;
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        result += U64_TERNARY(stash->overflow_blocks[i].id != EMPTY_BLOCK_ID, 1, 0);
    }
    return result;
}
static inline block* first_block_in_bucket_for_level(const stash* stash, size_t level) {
    return stash->path_blocks + level * BLOCKS_PER_BUCKET;
}

static void cond_copy_block(bool cond, block* dst, const block* src) {
    u64* tail_dst = (u64*)dst;
    u64* tail_src = (u64*)src;
    for(size_t i=0;i<sizeof(*dst)/sizeof(u64);++i) {
        cond_obv_cpy_u64(cond, tail_dst + i, tail_src + i);
    }
}

static void cond_swap_blocks(bool cond, block* a, block* b) { 
    u64* tail_dst = (u64*)a;
    u64* tail_src = (u64*)b;
    for(size_t i=0;i<sizeof(*a)/sizeof(u64);++i) {
        cond_obv_swap_u64(cond, tail_dst + i, tail_src + i);
    }
}

// Precondition: `target` is an empty block OR no block in the bucket has ID equal to `target_block_id`
// Postcondition: No block in the bucket has ID equal to `target_block_id`, `target` is either empty or `target->id == target_block_id`.
void stash_add_path_bucket(stash* stash, bucket_store* bucket_store, u64 bucket_id, u64 target_block_id, block *target) {
    size_t level = tree_path_level(bucket_id);
    block* bucket_blocks = first_block_in_bucket_for_level(stash, level);
    bucket_store_read_bucket_blocks(bucket_store, bucket_id, bucket_blocks);
    for(size_t i = 0; i < BLOCKS_PER_BUCKET; ++i) {
        bool cond = (target_block_id == bucket_blocks[i].id);
        CHECK(!(cond  & (target->id != EMPTY_BLOCK_ID)));
        cond_swap_blocks(cond, target, bucket_blocks + i);
    }
}


// Precondition: `target` is an empty block OR no block in the overflow has ID equal to `target_block_id`
// Postcondition: No block in the overflow has ID equal to `target_block_id`, `target` is either empty or `target->id == target_block_id`.
void stash_scan_overflow_for_target(stash* stash, u64 target_block_id, block *target) {
    size_t num_found = 0;
    size_t ub = stash_overflow_ub(stash);
    for(size_t i = 0; i < ub; ++i) {
        bool cond = (stash->overflow_blocks[i].id == target_block_id);
        CHECK(!(cond  & (target->id != EMPTY_BLOCK_ID)));
        cond_swap_blocks(cond, target, stash->overflow_blocks + i);
        num_found += cond ? 1 : 0;
    }
    CHECK(num_found <= 1);
}

// Precondition: there is no block with ID `new_block->id` anywhere in the stash - neither the path_Stash nor the overflow.
error_t stash_add_block(stash* stash, block* new_block) {
    bool inserted = false;
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        bool cond = (stash->overflow_blocks[i].id == EMPTY_BLOCK_ID) & !inserted;
        cond_copy_block(cond, stash->overflow_blocks + i, new_block);
        inserted = inserted | cond;
    }

    // This branch may leak information about the size of the stash but (1) this branch is difficult
    // to hit even with a malicious attack pattern and (2) we currently leak the stash size in much
    // more direct ways (we even publish it in the statistics), and if we decide to stop leaking
    // stash size we will have to stop extending the stash and simply fail. 
    if(!inserted) {
        stash_extend_overflow(stash);
        return stash_add_block(stash, new_block);
    }
    return err_SUCCESS;
}

static void stash_assign_block_to_bucket(stash* stash, const tree_path* path, block_type type, size_t index) {
    bool is_overflow_block = (type == block_type_overflow);

    // the block cannot be assigned to this level or higher 
    size_t max_level = U64_TERNARY(is_overflow_block, stash->path_length, (index / BLOCKS_PER_BUCKET) + 1);
    size_t assignment_index = U64_TERNARY(is_overflow_block,  BLOCKS_PER_BUCKET * stash->path_length  + index, index);
    block* block = stash->path_blocks + assignment_index;

    bool is_assigned = false;
    for(u64 level = 0; level < max_level; ++level) {
        u64 bucket_occupancy = stash->bucket_occupancy[level];
        u64 bucket_id = path->values[level];
        bool lower_bound = tree_path_lower_bound(bucket_id) <= block->position;
        bool upper_bound = tree_path_upper_bound(bucket_id) >= block->position;
        bool is_valid = lower_bound & upper_bound;
        bool bucket_has_room = bucket_occupancy < BLOCKS_PER_BUCKET;    
        bool cond = is_valid & bucket_has_room & !is_assigned & block->id != EMPTY_BLOCK_ID;

        // If `cond` is true, put it in the bucket: increment the bucket occupancy and set the bucket assignment
        // for this position.
        // increment this, it will only get saved if `cond` is true.
        ++bucket_occupancy;
        cond_obv_cpy_u64(cond, stash->bucket_occupancy + level, &bucket_occupancy);
        cond_obv_cpy_u64(cond, stash->bucket_assignments + assignment_index, &level);
        is_assigned = cond | is_assigned;
    }
}

static void stash_place_empty_blocks(stash* stash) {
    u64 curr_bucket = 0;

    
    for(size_t i = 0; i < stash->num_blocks; ++i) {
        bool found_curr_bucket = false;
        for(size_t j = 0; j < stash->path_length; ++j) {
            bool bucket_has_room = (stash->bucket_occupancy[j] != BLOCKS_PER_BUCKET);
            bool set_curr_bucket = bucket_has_room & !found_curr_bucket;
            cond_obv_cpy_u64(set_curr_bucket, &curr_bucket, &j);
            found_curr_bucket = set_curr_bucket | found_curr_bucket;
        }
        u64 bucket_occupancy = stash->bucket_occupancy[curr_bucket];
        bool cond_place_in_bucket = bucket_occupancy < BLOCKS_PER_BUCKET & stash->blocks[i].id == EMPTY_BLOCK_ID;
        bucket_occupancy++;

        cond_obv_cpy_u64(cond_place_in_bucket, stash->bucket_occupancy + curr_bucket, &bucket_occupancy);
        cond_obv_cpy_u64(cond_place_in_bucket, stash->bucket_assignments + i, &curr_bucket);

    }

    // at the end, every bucket should be full
}

static error_t stash_assign_buckets(stash* stash, const tree_path* path) {
    // assign all blocks to "overflow" - level UINT64_MAX and set all occupancy to 0
    memset(stash->bucket_assignments, 255, stash->num_blocks*sizeof(stash->bucket_assignments));
    memset(stash->bucket_occupancy, 0, stash->path_length * sizeof(*stash->bucket_occupancy));


    // assign blocks in path to buckets first
    for(size_t level = 0; level < stash->path_length; ++level) {
        for(size_t b = 0; b < BLOCKS_PER_BUCKET; ++b) {
            stash_assign_block_to_bucket(stash, path, block_type_path, level * BLOCKS_PER_BUCKET + b);
        }
    }

    // assign blocks in overflow to buckets
    size_t ub = stash_overflow_ub(stash);
    for(size_t i = 0; i < ub; ++i) {
        stash_assign_block_to_bucket(stash, path, block_type_overflow, i);
    }

    // now assign empty blocks to fill the buckets
    stash_place_empty_blocks(stash);
    return err_SUCCESS;
}


static inline bool comp_blocks(block* blocks, u64* block_level_assignments, size_t idx1, size_t idx2) {
    return (block_level_assignments[idx1] > block_level_assignments[idx2])
                | ((block_level_assignments[idx1] == block_level_assignments[idx2]) & (blocks[idx1].position > blocks[idx2].position));
}

/**
 * @brief Core subroutine for `bitonic_sort`. Takes a bitonic array as input and sorts it with a deterministic sequence
 * of conditional swaps.
 * 
 * @param blocks blocks to sort
 * @param block_level_assignments level assignments for blocks. Blocks will be sorted in order of increasing level
 * @param lb lower bound for array to sort
 * @param ub upper bound for array to sort (non-inclusive)
 * @param direction result is ascending sort if true, descending if false.
 */
static void bitonic_merge(block* blocks, u64* block_level_assignments, size_t lb, size_t ub, bool direction) {
    size_t n = ub - lb;
    if(n > 1) {
        size_t pow2 = first_pow2_leq(n);
        if(pow2 == n) pow2 >>= 1;
        for(size_t i = lb; i < ub - pow2; ++i) {
            bool cond = direction == comp_blocks(blocks, block_level_assignments, i, i+pow2);
            cond_swap_blocks(cond, blocks + i, blocks + i + pow2);
            cond_obv_swap_u64(cond, block_level_assignments + i, block_level_assignments + i + pow2);
        }

        // Note that at this point, since the input was bitonic, everything in the arraw with
        // index >= lb + pow2 has a "larger" value (relative to `direction`) than the entries
        // with index < lb + pow2. Also, both the upper and lower part of the array are bitonic
        // subarrays.
        bitonic_merge(blocks, block_level_assignments, lb, lb + pow2, direction);
        bitonic_merge(blocks, block_level_assignments, lb + pow2, ub, direction);
    }
}

/**
 * @brief Bitonic (as opposed to monotonic) sort is an oblivious sort algorithm due to Batcher (http://www.cs.kent.edu/~batcher/sort.pdf).
 * While initially designed for building sorting circuits in hardward, it serves our purposes here because it will sort a list
 * using a deterministic sequence of comparisons and swaps - independent of the input. It is called "bitonic" because its core
 * subroutine `bitonic_merge` takes a bitonic list (only changes direction once, e.g., increasing and then decreasing) and converts it to a monotonic
 * (i.e. sorted) list.
 * 
 * @param blocks blocks to sort
 * @param block_level_assignments level assignments for blocks. Blocks will be sorted in order of increasing level
 * @param lb lower bound for array to sort
 * @param ub upper bound for array to sort (non-inclusive)
 * @param direction Ascending sort if true, descending if false.
 */
static void bitonic_sort(block* blocks, u64* block_level_assignments, size_t lb, size_t ub, bool direction) {
    size_t n = ub - lb;
    if(n > 1) {
        size_t half_n = n>>1;
        bitonic_sort(blocks, block_level_assignments, lb, lb + half_n, !direction);
        bitonic_sort(blocks, block_level_assignments, lb + half_n, ub, direction);
        bitonic_merge(blocks, block_level_assignments, lb, ub, direction);
    }
}


void print_bucket_assignments(const stash* stash) {
    for(size_t i = 0; i < stash->num_blocks; ++i) {
        fprintf(stderr, "%zu: block: %" PRIu64 " pos: %" PRIu64 " assignment: %" PRIu64 "\n",
            i, stash->blocks[i].id, stash->blocks[i].position, stash->bucket_assignments[i]);
    }
}

void stash_build_path(stash* stash, const tree_path* path) {
    size_t overflow_size = stash_overflow_ub(stash);
    stash_assign_buckets(stash, path);
    bitonic_sort(stash->blocks, stash->bucket_assignments, 0, BLOCKS_PER_BUCKET * stash->path_length + overflow_size, true);
    // print_bucket_assignments(stash);
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


    cond_copy_block(false, &target, &b1);
    TEST_ASSERT(memcmp(&mt_block, &target, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b1_orig, &b1, sizeof(block)) == 0);

    cond_copy_block(true, &target, &b1);
    TEST_ASSERT(memcmp(&target, &b1, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b1_orig, &b1, sizeof(block)) == 0);

    cond_swap_blocks(false, &b1, &b2);
    TEST_ASSERT(memcmp(&b1_orig, &b1, sizeof(block)) == 0);
    TEST_ASSERT(memcmp(&b2_orig, &b2, sizeof(block)) == 0);

    cond_swap_blocks(true, &b1, &b2);
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

    bitonic_sort(blocks, bucket_assignments, 0, num_blocks, true);
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
    u64 lb = tree_path_lower_bound(bucket_id);
    u64 ub = tree_path_upper_bound(bucket_id);
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
        bucket_store_write_bucket_blocks(bucket_store, path->values[level], bucket_blocks);
    }
    
}

int test_load_bucket_path_to_stash(bucket_density density) {
    size_t num_levels = 18;
    stash *stash = stash_create(num_levels, TEST_STASH_SIZE);
    bucket_store* bucket_store = bucket_store_create(num_levels);
    u64 root = (1ul << (num_levels - 1)) - 1;
    u64 leaf = 157142;
    tree_path* path = tree_path_create(leaf, root);
    size_t num_blocks_added = 0;
    load_bucket_store(bucket_store, num_levels, path, density, &num_blocks_added);

    u64 target_block_id = num_blocks_added / 2;
    block target = {.id=EMPTY_BLOCK_ID};
    for(size_t i = 0; i < path->length; ++i) {
        stash_add_path_bucket(stash, bucket_store, path->values[i], target_block_id, &target);
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

    RETURN_IF_ERROR(stash_add_block(stash, &b0));
    RETURN_IF_ERROR(stash_add_block(stash, &b1));
    bool b0_in_stash = false;
    bool b1_in_stash = false;
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        b0_in_stash = b0_in_stash || (stash->overflow_blocks[i].id == b0.id);
        b1_in_stash = b1_in_stash || (stash->overflow_blocks[i].id == b1.id);
    }
    TEST_ASSERT(b0_in_stash);
    TEST_ASSERT(b1_in_stash);

    stash_scan_overflow_for_target(stash, block_id0, &target0);
    stash_scan_overflow_for_target(stash, block_id1, &target1);

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
        RETURN_IF_ERROR(stash_add_block(stash, &b));
        // check that it is in the stash
    }

    block b = {.id = stash->overflow_capacity, .position = 2*(100+stash->overflow_capacity)};

    // This will trigger an extension of the stash
    RETURN_IF_ERROR(stash_add_block(stash, &b));

    // now remove a block and then confirm that we have room
    block target = {.id = EMPTY_BLOCK_ID, .position = UINT64_MAX};

    u64 search_block_id = 11;
    stash_scan_overflow_for_target(stash, search_block_id, &target);
    TEST_ASSERT(target.id == search_block_id);
    for(size_t i = 0; i < stash->overflow_capacity; ++i) {
        TEST_ASSERT(stash->blocks[i].id != search_block_id);
    }
    RETURN_IF_ERROR(stash_add_block(stash, &b));

    stash_destroy(stash);
    return 0;
}

#endif // IS_TEST
