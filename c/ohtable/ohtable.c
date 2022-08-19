// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

// included to print the max_trace_length when it grows
#include <stdio.h>

#include "path_oram/path_oram.h"
#include "path_oram/bucket.h"
#include "ohtable.h"

#include "util/util.h"

struct ohtable
{
    oram *oram;
    size_t capacity;
    size_t record_size_qwords;
    size_t items_per_block;
    u64 base_block_id;
    size_t num_blocks;
    u64* temp_record;

    // stats and robinhood help
    size_t max_offset;
    size_t max_trace_length;
    size_t total_displacement;
    size_t num_items;
    u64 *swap_buf;

    // constant-time mod precomputations
    u64 capacity_ct_m_prime;
    size_t capacity_ct_shift1;
    size_t capacity_ct_shift2;
    u64 items_per_block_ct_m_prime;
    size_t items_per_block_ct_shift1;
    size_t items_per_block_ct_shift2;
};

typedef struct
{
    u64 block_id;
    size_t index;
} location;

static size_t fnv1a64(size_t size, uint8_t *data)
{
    // http://isthe.com/chongo/tech/comp/fnv/
    uint64_t fnv_offset_basis = 14695981039346656037UL;
    uint64_t fnv_prime = 1099511628211;

    uint64_t hash = fnv_offset_basis;
    for (size_t i = 0; i < size; ++i)
    {
        uint64_t byte = data[i];
        hash = hash ^ byte;
        hash *= fnv_prime;
    }

    return (size_t)hash;
}

static inline u64 raw_hash(u64 key) {
    return fnv1a64(8, (uint8_t *)&key);
}

static location hash(u64 key, size_t capacity, size_t items_per_block, 
        u64 capacity_ct_m_prime, size_t capacity_ct_shift1, size_t capacity_ct_shift2,
        u64 items_per_block_ct_m_prime, size_t items_per_block_ct_shift1, size_t items_per_block_ct_shift2)
{
    size_t h = ct_mod(raw_hash(key), capacity, capacity_ct_m_prime, capacity_ct_shift1, capacity_ct_shift2);

    u64 block_id =  ct_div(h, items_per_block, items_per_block_ct_m_prime, items_per_block_ct_shift1, items_per_block_ct_shift2);
    size_t idx = ct_mod(h, items_per_block, items_per_block_ct_m_prime, items_per_block_ct_shift1, items_per_block_ct_shift2);
    location result = {.block_id = block_id, .index = idx};
    return result;
}

static location ohtable_hash(const ohtable *ohtable, u64 key)
{
    return hash(key, ohtable->capacity, ohtable->items_per_block, 
            ohtable->capacity_ct_m_prime, ohtable->capacity_ct_shift1, ohtable->capacity_ct_shift2,
            ohtable->items_per_block_ct_m_prime, ohtable->items_per_block_ct_shift1, ohtable->items_per_block_ct_shift2);
}

static size_t ohtable_hash_from_location(const ohtable *ohtable, location loc)
{
    return loc.block_id * ohtable->items_per_block + loc.index;
}

/**
 * @brief measure the forward distance from a hash value to another hash. This is the size of
 * jump needed to get from the hash value starting point to get to the location
 *
 * @param ohtable_capacity Number of records the `ohtable` can hold
 * @param hash starting point for a linear proble search
 * @param loc location to measure
 * @return size_t
 */
static size_t ohtable_distance_to_hash(size_t ohtable_capacity, size_t hash, size_t target_hash) {
    return target_hash < hash
               ? target_hash + ohtable_capacity - hash // The search wrapped around
               : target_hash - hash;

}

static ohtable* _create(oram* oram, size_t record_size_qwords)
{
    size_t items_per_block = BLOCK_DATA_SIZE_QWORDS / record_size_qwords;
    size_t num_blocks = oram_capacity_blocks(oram);

    ohtable *result = 0;

    CHECK(result = calloc(1, sizeof(*result)));
    result->oram = oram;
    result->capacity = num_blocks * items_per_block;
    result->num_blocks = num_blocks;
    result->items_per_block = items_per_block;
    result->record_size_qwords = record_size_qwords;

    prep_ct_div(result->capacity, &result->capacity_ct_m_prime, &result->capacity_ct_shift1, &result->capacity_ct_shift2);
    prep_ct_div(result->items_per_block, &result->items_per_block_ct_m_prime, &result->items_per_block_ct_shift1, &result->items_per_block_ct_shift2);

    result->base_block_id = oram_allocate_contiguous(oram, num_blocks);

    CHECK(result->swap_buf = calloc(record_size_qwords, sizeof(*(result->swap_buf))));
    CHECK(result->temp_record = calloc(record_size_qwords, sizeof(*(result->temp_record))));
    
    return result;
}

ohtable* ohtable_create_for_available_mem(size_t record_size_qwords, size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy) {
    // The `ohtable` struct contains two buffers to hold records during processing,
    // `swap_buf` and `temp_record`. So the size used by this structure is 
    // the size of the struct plus the size of 2 records.
    available_bytes -= sizeof(ohtable) - 2*record_size_qwords*sizeof(u64);
    oram* oram = oram_create_for_available_mem(available_bytes, load_factor, stash_overflow_size, getentropy);
    return _create(oram, record_size_qwords);
}

ohtable *ohtable_create(size_t record_capacity, size_t record_size_qwords, size_t stash_overflow_size, entropy_func getentropy)
{
    size_t items_per_block = BLOCK_DATA_SIZE_QWORDS / record_size_qwords;
    size_t num_blocks = (record_capacity / items_per_block) + (record_capacity % items_per_block == 0 ? 0 : 1);
    size_t capacity = num_blocks * BLOCK_DATA_SIZE_QWORDS;

    oram *oram = oram_create(capacity, stash_overflow_size, getentropy);
    return _create(oram, record_size_qwords);
}

void ohtable_destroy(ohtable *ohtable)
{
    if (ohtable)
    {
        oram_destroy(ohtable->oram);
        free(ohtable->swap_buf);
        free(ohtable->temp_record);
        free(ohtable);
    }
}

void ohtable_clear(ohtable *ohtable)
{
    oram_clear(ohtable->oram);
    ohtable->base_block_id = oram_allocate_contiguous(ohtable->oram, ohtable->num_blocks);

    ohtable->max_trace_length = 0;
    ohtable->total_displacement = 0;
    ohtable->num_items = 0;
}

ohtable_statistics* ohtable_statistics_create(ohtable* ohtable) {
    ohtable_statistics* stats;
    CHECK(stats = calloc(1, sizeof(*stats)));
    stats->num_items = ohtable->num_items;
    stats->capacity = ohtable->capacity;
    stats->max_trace_length = ohtable->max_trace_length;
    stats->total_displacement = ohtable->total_displacement;
    
    // ORAM stats
    const oram_statistics* oram_stats = oram_report_statistics(ohtable->oram);
    stats->oram_access_count = oram_stats->access_count;
    stats->oram_recursion_depth = oram_stats->recursion_depth;
    stats->max_stash_overflow_count = oram_stats->max_stash_overflow_count;
    stats->stash_overflow_count = oram_stats->stash_overflow_count;
    stats->sum_stash_overflow_count = oram_stats->sum_stash_overflow_count;
    stats->stash_overflow_ema10k = oram_stats->stash_overflow_ema10k;
    stats->posmap_max_stash_overflow_count = oram_stats->posmap_max_stash_overflow_count;
    stats->posmap_stash_overflow_count = oram_stats->posmap_stash_overflow_count;
    stats->posmap_sum_stash_overflow_count = oram_stats->posmap_sum_stash_overflow_count;
    stats->posmap_stash_overflow_ema10k = oram_stats->posmap_stash_overflow_ema10k;
    return stats;
}


void ohtable_statistics_destroy(ohtable_statistics* stats){
    if(stats) {
        free(stats);
    }
}

size_t ohtable_capacity(const ohtable* ohtable) {
    return ohtable->capacity;
}

size_t ohtable_num_items(const ohtable* ohtable) {
    return ohtable->num_items;
}

double ohtable_mean_displacement(const ohtable *ohtable)
{
    return ((double)ohtable->total_displacement) / ohtable->num_items;
}

inline static bool record_empty(const u64 record[])
{
    return record[0] == UINT64_MAX;
}

inline static bool record_empty_or_matches(const u64 record[], u64 key)
{
    return record_empty(record) | (record[0] == key);
}



static void cond_copy_record(bool cond, u64* dest, const u64* src, size_t record_size_qwords) {
    for(size_t i = 0; i < record_size_qwords; ++i) {
        cond_obv_cpy_u64(cond, dest + i, src + i);
    }
}

static void cond_swap_record(bool cond, u64* a, u64* b, size_t record_size_qwords) {
    for(size_t i = 0; i < record_size_qwords; ++i) {
        cond_obv_swap_u64(cond, a + i, b + i);
    }
}

typedef struct {
    size_t record_size_qwords;
    size_t start_index;

    // set to true when all insertion, including swapped records, is complete
    bool insert_complete;

    // record that needs to be inserted. This may not be the record that was originally upserted.
    // That record might have been swapped for another to produce a better placement.
    u64* in_record;

    // The jump from the hash of `in_record` to the slot currently being considered
    size_t curr_jump;
    // the hash (modulo table capacity) of `in_record` - the record that currently needs to be inserted
    u64 curr_record_hash;
    // the hash (modulo table capacity) that would place an item in the slot currently being considered
    u64 curr_slot_hash;
    // number of steps taken so far in this upsert
    size_t trace;
    // true if the originally upserted item was not present, i.e. it was an insert, not an update.
    bool inserted_new_item;   
    // The maximum offset from hash value for all items stored (including swaps) in this access.
    size_t max_offset;
    
    size_t ohtable_capacity;

    // constant-time mod precomputations
    u64 capacity_ct_m_prime;
    size_t capacity_ct_shift1;
    size_t capacity_ct_shift2;
} robinhood_accessor_args;

static error_t robinhood_accessor(u64* block_data, void* vargs) {
    robinhood_accessor_args* args = vargs;
    size_t records_per_block = BLOCK_DATA_SIZE_QWORDS / args->record_size_qwords;
    bool inserted = false;
    
    for(size_t i = 0; i < records_per_block; ++i) {
        u64* record = args->in_record;
        u64 key = record[0];
        u64* candidate_record = block_data + i * args->record_size_qwords;

        bool search_started = (i >= args->start_index);
        bool should_store = search_started & record_empty_or_matches(candidate_record, key) & !args->insert_complete;

        u64 candidate_hash = ct_mod(raw_hash(candidate_record[0]), args->ohtable_capacity, args->capacity_ct_m_prime, args->capacity_ct_shift1, args->capacity_ct_shift2);
        size_t candidate_jump = ohtable_distance_to_hash(args->ohtable_capacity, candidate_hash, args->curr_slot_hash);

        // to decide if we swap we see which has the smaller jump. If it's a tie, we break it by looking at
        // the full hash. (Candidate_hash and curr_record_hash are taken modulo the table capacity)
        bool jump_smaller = (candidate_jump < args->curr_jump)
                | ((candidate_jump == args->curr_jump) 
                    & (raw_hash(candidate_record[0]) < raw_hash(record[0])));
        bool should_swap = search_started & !should_store & !args->insert_complete & jump_smaller;
        
        args->inserted_new_item = args->inserted_new_item 
                | (!inserted & (should_swap | (should_store & record_empty(candidate_record))));

        cond_copy_record(should_store, candidate_record, record, args->record_size_qwords);
        cond_swap_record(should_swap, candidate_record, record, args->record_size_qwords);

        
        inserted = inserted | should_store | should_swap;
        args->insert_complete = args->insert_complete | should_store;

        args->max_offset = U64_TERNARY(should_swap | should_store & (args->curr_jump > args->max_offset), args->curr_jump, args->max_offset);
        args->curr_record_hash = U64_TERNARY(should_swap, candidate_hash, args->curr_record_hash);
        args->curr_jump = U64_TERNARY(should_swap, candidate_jump, args->curr_jump);

        args->curr_slot_hash += U64_TERNARY(search_started, 1, 0);
        u64 inc = U64_TERNARY(search_started & !args->insert_complete, 1, 0);
        args->trace += inc;
        args->curr_jump += inc;        
    }

    return err_SUCCESS;
}

// we will declare a table full at 98% capacity when performance is already badly degraded
static inline bool is_full(const ohtable* ohtable) {
    // does not need constant time division because operates on non-secret data
    return ((double)ohtable->num_items)/ohtable->capacity > 0.98;
}

error_t ohtable_put(ohtable *ohtable, const u64 record[])
{
    if(is_full(ohtable)) {
        // An update would still be OK but we stop all upserts regardless.
        return err_OHTABLE__TABLE_FULL;
    }

    location loc = ohtable_hash(ohtable, record[0]);
    u64 block_id = loc.block_id;
    memcpy(ohtable->temp_record, record, ohtable->record_size_qwords * sizeof(record[0]));

    size_t curr_record_hash = ohtable_hash_from_location(ohtable, loc);
    u64 curr_slot_hash = curr_record_hash;

    robinhood_accessor_args args = {
        .start_index =  loc.index, 
        .insert_complete = false, 
        .in_record = ohtable->temp_record, 
        .curr_record_hash = curr_record_hash,
        .inserted_new_item = false,
        .ohtable_capacity = ohtable->capacity,
        .record_size_qwords = ohtable->record_size_qwords,
        .curr_slot_hash = curr_slot_hash,
        .capacity_ct_m_prime = ohtable->capacity_ct_m_prime,
        .capacity_ct_shift1 = ohtable->capacity_ct_shift1,
        .capacity_ct_shift2 = ohtable->capacity_ct_shift2};

    while(!args.insert_complete) {
        RETURN_IF_ERROR(oram_function_access(ohtable->oram, block_id, robinhood_accessor, &args));
        block_id = U64_TERNARY(block_id + 1 == ohtable->num_blocks, 0, block_id+1);
        args.start_index = 0;
    }
    ohtable->num_items += U64_TERNARY(args.inserted_new_item, 1, 0);
    ohtable->total_displacement += args.trace;
    ohtable->max_trace_length = U64_TERNARY(args.trace > ohtable->max_trace_length, args.trace, ohtable->max_trace_length);
    ohtable->max_offset = U64_TERNARY(args.max_offset > ohtable->max_offset, args.max_offset, ohtable->max_offset);
    
#ifdef IS_TEST
    if (args.trace > ohtable->max_trace_length)
    {
        TEST_LOG("Max trace length: %ld", args.trace);
    }
    if(args.max_offset > ohtable->max_offset) {
        TEST_LOG("Max offset: %ld", args.max_offset);
    }
#endif // IS_TEST
    return err_SUCCESS;

}


typedef struct {
    size_t record_size_qwords;
    size_t start_index;
    bool search_complete;
    u64* out_record;
    u64 key;
} get_record_accessor_args;

static error_t get_record_accessor(u64* block_data, void* vargs) {
    get_record_accessor_args* args = vargs;
    size_t records_per_block = BLOCK_DATA_SIZE_QWORDS / args->record_size_qwords;
    for(size_t i = 0; i < records_per_block; ++i) {
        u64* candidate_record = block_data + i * args->record_size_qwords;
        bool search_started = (i >= args->start_index);
        bool should_copy = search_started & record_empty_or_matches(candidate_record, args->key) 
            & !args->search_complete;
        cond_copy_record(should_copy, args->out_record, candidate_record, args->record_size_qwords);
        args->search_complete = args->search_complete | should_copy;
    }

    return err_SUCCESS;

}

error_t ohtable_get(const ohtable *ohtable, u64 key, u64 record[])
{
    // Compute the worst case number of blocks that must be scanned in order to find a record
    // and scan that many blocks for every request. We know the maximum offset of a record from
    // its hash, `ohtable->max_offset`. If the hash is the last record in a block then: (1) if 
    // max_offset is 0 then we only scan one block. (2) if 1 <= max_offset && max_offset <= items_per_block
    // we scan two blocks, etc.
    size_t blocks_to_scan = 1 + (ohtable->max_offset + ohtable->items_per_block-1)/ohtable->items_per_block;
    location loc = ohtable_hash(ohtable, key);
    u64 block_id = loc.block_id;
    get_record_accessor_args args = {
        .key = key, 
        .start_index = loc.index, 
        .search_complete = false, 
        .out_record = record, 
        .record_size_qwords = ohtable->record_size_qwords};
        
    while(blocks_to_scan > 0) {
        RETURN_IF_ERROR(oram_function_access(ohtable->oram, block_id, get_record_accessor, &args));
        block_id = U64_TERNARY(block_id + 1 == ohtable->num_blocks, 0, block_id+1);
        args.start_index = 0;
        --blocks_to_scan;
    }
    return err_SUCCESS;
}

#ifdef IS_TEST
#include <stdio.h>
#include <sys/random.h>

#include "util/util.h"
#include "util/tests.h"

int test_hash_distance()
{
    ohtable *table = ohtable_create((BLOCK_DATA_SIZE_QWORDS / 7) * 16, 7, TEST_STASH_SIZE, getentropy);
    TEST_ASSERT(table->capacity == 16 * table->items_per_block);

    location loc1 = {.block_id = 0, .index = 5};
    location loc2 = {.block_id = 5, .index = 0};
    u64 h1 = ohtable_hash_from_location(table, loc1);
    u64 h2 = ohtable_hash_from_location(table, loc2);

    TEST_ASSERT(ohtable_distance_to_hash(table->capacity, 3, h1) == 2);
    TEST_ASSERT(ohtable_distance_to_hash(table->capacity, table->capacity - 2, h1) == 7);

    TEST_ASSERT(ohtable_distance_to_hash(table->capacity, table->capacity - 2, h2) == 5 * table->items_per_block + 2);
    TEST_ASSERT(ohtable_distance_to_hash(table->capacity, 5 * table->items_per_block - 1, h2) == 1);
    ohtable_destroy(table);
    return 0;
}


u64 find_collision_for_hash(u64 h, size_t table_capacity, bool new_item_wins) {
    u64 candidate_key;
    u64 candidate_hash;
    bool tie_breaker = false;
    bool found_match = false;
    
    while(!found_match) {
        getentropy(&candidate_key, sizeof(candidate_key));
        candidate_hash = raw_hash(candidate_key);
        tie_breaker = candidate_hash > h;
        found_match = (candidate_hash % table_capacity) == (h % table_capacity);
        if(found_match) {
            found_match = (new_item_wins && tie_breaker) || (!new_item_wins && !tie_breaker);
        }
    }
    return candidate_key;

}

#define RECORD_SIZE_QWORDS 7
error_t test_rh_accessor_placement() {
    u64 block_data[BLOCK_DATA_SIZE_QWORDS];
    memset(block_data, 255, sizeof(block_data));
    size_t record_size_qwords = RECORD_SIZE_QWORDS;
    size_t records_per_block = BLOCK_DATA_SIZE_QWORDS / record_size_qwords;
    size_t capacity = 16*records_per_block;


    u64 keys[1 + BLOCK_DATA_SIZE_QWORDS / RECORD_SIZE_QWORDS];
    u64 capacity_ct_m_prime;
    size_t capacity_ct_shift1;
    size_t capacity_ct_shift2;
    u64 ipb_ct_m_prime;
    size_t ipb_ct_shift1;
    size_t ipb_ct_shift2;
    prep_ct_div(capacity, &capacity_ct_m_prime, &capacity_ct_shift1, &capacity_ct_shift2);
    prep_ct_div(records_per_block, &ipb_ct_m_prime, &ipb_ct_shift1, &ipb_ct_shift2);
    
    // first get keys that will get placed in the right slots
    u64 base = ((UINT64_MAX >> 1) / capacity) * capacity;
    for(size_t i = 0; i <= records_per_block; ++i) {
        keys[i] = find_collision_for_hash(base + i, capacity, true);
        location loc = hash(keys[i], capacity, records_per_block, 
                capacity_ct_m_prime, capacity_ct_shift1, capacity_ct_shift2,
                ipb_ct_m_prime, ipb_ct_shift1, ipb_ct_shift2);
        if(i < records_per_block) {
            TEST_ASSERT(loc.block_id == 0);
            TEST_ASSERT(loc.index == i);
        } else {
            TEST_ASSERT(loc.block_id == 1);
            TEST_ASSERT(loc.index == 0);
        }
    }


    // now insert records in the block for each key that should fit
    u64 temp_record[RECORD_SIZE_QWORDS];
    for(size_t i = 0; i < records_per_block; ++i) {
        memset(temp_record, 0, record_size_qwords * sizeof(*temp_record));
        temp_record[0] = keys[i];
        u64 curr_record_hash = raw_hash(temp_record[0]) % capacity;
        u64 curr_slot_hash = curr_record_hash;

        robinhood_accessor_args args = {
            .start_index = i, 
            .insert_complete = false, 
            .in_record = temp_record, 
            .curr_record_hash = curr_record_hash,
            .inserted_new_item = false,
            .ohtable_capacity = capacity,
            .record_size_qwords = record_size_qwords,
            .curr_slot_hash = curr_slot_hash,
            .capacity_ct_m_prime = capacity_ct_m_prime,
            .capacity_ct_shift1 = capacity_ct_shift1,
            .capacity_ct_shift2 = capacity_ct_shift2};

        RETURN_IF_ERROR(robinhood_accessor(block_data, &args));
        TEST_ASSERT(args.insert_complete);
        TEST_ASSERT(args.inserted_new_item);

        TEST_ASSERT(args.trace == 0);
        TEST_ASSERT(args.curr_jump == 0);
    }

    for(size_t i = 0; i < records_per_block; ++i) {
        TEST_ASSERT(block_data[i*record_size_qwords] == keys[i]);
    }

    // now insert one that doesn't fit
    {
        u64 new_key = find_collision_for_hash(raw_hash(keys[records_per_block - 1]), capacity, true);

        memset(temp_record, 0, record_size_qwords * sizeof(*temp_record));
        temp_record[0] = new_key;
        u64 curr_record_hash = raw_hash(temp_record[0]) % capacity;
        u64 curr_slot_hash = curr_record_hash;
        robinhood_accessor_args args = {
            .start_index = records_per_block - 1, 
            .insert_complete = false, 
            .in_record = temp_record, 
            .curr_record_hash = curr_record_hash,
            .inserted_new_item = false,
            .ohtable_capacity = capacity,
            .record_size_qwords = record_size_qwords,
            .curr_slot_hash = curr_slot_hash,
            .capacity_ct_m_prime = capacity_ct_m_prime,
            .capacity_ct_shift1 = capacity_ct_shift1,
            .capacity_ct_shift2 = capacity_ct_shift2};
        RETURN_IF_ERROR(robinhood_accessor(block_data, &args));

        TEST_ASSERT(block_data[(records_per_block - 1) * record_size_qwords] == new_key);
        TEST_ASSERT(temp_record[0] == keys[records_per_block - 1]);
        TEST_ASSERT(!args.insert_complete);
        TEST_ASSERT(args.inserted_new_item);

        TEST_ASSERT(args.trace == 1);
        TEST_ASSERT(args.curr_jump == 1);
    }
    return err_SUCCESS;
}

#define SWAP_CHAIN_LENGTH 5
error_t test_multiswap() {
    u64 block_data[BLOCK_DATA_SIZE_QWORDS];
    memset(block_data, 255, sizeof(block_data));
    size_t record_size_qwords = RECORD_SIZE_QWORDS;
    size_t records_per_block = BLOCK_DATA_SIZE_QWORDS / record_size_qwords;
    size_t capacity = 16*records_per_block;

    u64 keys[SWAP_CHAIN_LENGTH];
    u64 capacity_ct_m_prime;
    size_t capacity_ct_shift1;
    size_t capacity_ct_shift2;
    prep_ct_div(capacity, &capacity_ct_m_prime, &capacity_ct_shift1, &capacity_ct_shift2);

    // first get keys that will get placed in the right slots
    u64 base = ((UINT64_MAX >> 1) / capacity) * capacity;
    size_t chain_start = 10;
    for(size_t i = 0; i < SWAP_CHAIN_LENGTH; ++i) {
        // get a key that will hash to the i-th position in the clock
        keys[i] = find_collision_for_hash(base + chain_start + i, capacity, true);
    }

    // Now insert records for those keys

    u64 temp_record[RECORD_SIZE_QWORDS];
    for(size_t i = 0; i < SWAP_CHAIN_LENGTH; ++i) {
        memset(temp_record, 0, record_size_qwords * sizeof(*temp_record));
        temp_record[0] = keys[i];
        u64 curr_record_hash = raw_hash(temp_record[0]) % capacity;
        u64 curr_slot_hash = curr_record_hash;

        robinhood_accessor_args args = {
            .start_index = chain_start + i, 
            .insert_complete = false, 
            .in_record = temp_record, 
            .curr_record_hash = curr_record_hash,
            .inserted_new_item = false,
            .ohtable_capacity = capacity,
            .record_size_qwords = record_size_qwords,
            .curr_slot_hash = curr_slot_hash,
            .capacity_ct_m_prime = capacity_ct_m_prime,
            .capacity_ct_shift1 = capacity_ct_shift1,
            .capacity_ct_shift2 = capacity_ct_shift2};

        RETURN_IF_ERROR(robinhood_accessor(block_data, &args));
        TEST_ASSERT(args.insert_complete);
        TEST_ASSERT(args.inserted_new_item);

        TEST_ASSERT(args.trace == 0);
        TEST_ASSERT(args.curr_jump == 0);
    }

    // check that they are in the right positions
    for(size_t i = 0; i < SWAP_CHAIN_LENGTH; ++i) {
        TEST_ASSERT(block_data[(chain_start + i)*RECORD_SIZE_QWORDS] == keys[i]);
    }

    // now insert an item at the start that will displace all of them
    u64 new_key = find_collision_for_hash(raw_hash(keys[0]), capacity, true);
    {
        TEST_LOG("new_key hash: %" PRIu64 " old key hash: %" PRIu64 "", raw_hash(new_key)%capacity, raw_hash(keys[0])%capacity);
        memset(temp_record, 0, record_size_qwords * sizeof(*temp_record));
        temp_record[0] = new_key;
        u64 curr_record_hash = raw_hash(temp_record[0]) % capacity;
        u64 curr_slot_hash = curr_record_hash;

        robinhood_accessor_args args = {
            .start_index = chain_start, 
            .insert_complete = false, 
            .in_record = temp_record, 
            .curr_record_hash = curr_record_hash,
            .inserted_new_item = false,
            .ohtable_capacity = capacity,
            .record_size_qwords = record_size_qwords,
            .curr_slot_hash = curr_slot_hash,
            .capacity_ct_m_prime = capacity_ct_m_prime,
            .capacity_ct_shift1 = capacity_ct_shift1,
            .capacity_ct_shift2 = capacity_ct_shift2};

        RETURN_IF_ERROR(robinhood_accessor(block_data, &args));
        TEST_ASSERT(args.insert_complete);
        TEST_ASSERT(args.inserted_new_item);

        TEST_LOG("trace length: %zu curr_jump: %zu", args.trace, args.curr_jump);
        TEST_ASSERT(args.trace == SWAP_CHAIN_LENGTH);
        TEST_ASSERT(args.curr_jump == 1);
    }

    // check that they are all shifted by 1
    TEST_ASSERT(block_data[chain_start*RECORD_SIZE_QWORDS] == new_key);
    for(size_t i = 0; i < SWAP_CHAIN_LENGTH; ++i) {
        TEST_ASSERT(block_data[(chain_start + i + 1)*RECORD_SIZE_QWORDS] == keys[i]);
    }

    return err_SUCCESS;
}

#define NUM_COLLISIONS 6
error_t test_wraparound() {
    ohtable *table = ohtable_create((BLOCK_DATA_SIZE_QWORDS / RECORD_SIZE_QWORDS) * 16, RECORD_SIZE_QWORDS, TEST_STASH_SIZE, getentropy);
    TEST_ASSERT(table->capacity == 16 * table->items_per_block);
    size_t capacity = table->capacity;

    // find a key that maps into the last block
    u64 base = ((UINT64_MAX >> 1) / capacity) * capacity;
    u64 key = find_collision_for_hash(base + capacity - NUM_COLLISIONS / 2, capacity, true);
    u64 keys[10];

    // generate collisions that will force swapping
    for(size_t i = 0; i < NUM_COLLISIONS; ++i) {
        u64 record[RECORD_SIZE_QWORDS];
        memset(record, 0, sizeof(record));
        record[0] = key;
        record[1] = i;
        u64 curr_record_hash = raw_hash(key);
        RETURN_IF_ERROR(ohtable_put(table, record));
        keys[i] = key;
        key = find_collision_for_hash(curr_record_hash, capacity, true);
    }

    for(size_t i = 0; i < NUM_COLLISIONS; ++i) {
        u64 record[RECORD_SIZE_QWORDS];
        RETURN_IF_ERROR(ohtable_get(table, keys[i], record));
        TEST_ASSERT(record[0] == keys[i]);
        TEST_ASSERT(record[1] == i);

    }
    ohtable_destroy(table);
    return err_SUCCESS;
}

error_t test_create_for_available_memory() {
    size_t memsize_1GiB = (1ul << 30);
    ohtable* table1 = ohtable_create_for_available_mem(RECORD_SIZE_QWORDS, memsize_1GiB, 1.0, TEST_STASH_SIZE, getentropy);
    size_t capacity1 = table1->capacity;
    ohtable_destroy(table1);


    ohtable* table2 = ohtable_create_for_available_mem(RECORD_SIZE_QWORDS, memsize_1GiB, 2.0, TEST_STASH_SIZE, getentropy);
    size_t capacity2 = table2->capacity;
    ohtable_destroy(table2);

    TEST_LOG("cap1: %zu cap2: %zu", capacity1, capacity2);
    TEST_ASSERT(capacity1 > 1500000);
    TEST_ASSERT(capacity2 == 2*capacity1);
    return err_SUCCESS;
}

void private_ohtable_tests()
{
    RUN_TEST(test_hash_distance());
    RUN_TEST(test_rh_accessor_placement());
    RUN_TEST(test_multiswap());
    RUN_TEST(test_wraparound());
    RUN_TEST(test_create_for_available_memory());
}
#endif
