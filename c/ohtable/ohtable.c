// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// included to print the max_trace_length when it grows
#include <stdio.h>

#include "path_oram/path_oram.h"
#include "path_oram/bucket.h"
#include "ohtable.h"

#include "util/util.h"

// struct ohtable layout must match Jasmin definitions in ohtable.jinc.
// Jasmin is the source of truth; validated at runtime by ohtable_validate_layout().
struct ohtable
{
    oram *oram;
    size_t capacity;
    size_t record_size_qwords;
    size_t items_per_block;
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

// Jasmin-exported layout functions for ohtable
extern size_t ohtable_sizeof_jazz(void);
extern size_t ohtable_capacity_offset_jazz(void);
extern size_t ohtable_items_per_block_offset_jazz(void);
extern size_t ohtable_capacity_ct_m_prime_offset_jazz(void);
extern size_t ohtable_capacity_ct_shift1_offset_jazz(void);
extern size_t ohtable_capacity_ct_shift2_offset_jazz(void);
extern size_t ohtable_items_per_block_ct_m_prime_offset_jazz(void);
extern size_t ohtable_items_per_block_ct_shift1_offset_jazz(void);
extern size_t ohtable_items_per_block_ct_shift2_offset_jazz(void);

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void ohtable_validate_layout(void) {
    CHECK(offsetof(ohtable, capacity) == ohtable_capacity_offset_jazz());
    CHECK(offsetof(ohtable, items_per_block) == ohtable_items_per_block_offset_jazz());
    CHECK(offsetof(ohtable, capacity_ct_m_prime) == ohtable_capacity_ct_m_prime_offset_jazz());
    CHECK(offsetof(ohtable, capacity_ct_shift1) == ohtable_capacity_ct_shift1_offset_jazz());
    CHECK(offsetof(ohtable, capacity_ct_shift2) == ohtable_capacity_ct_shift2_offset_jazz());
    CHECK(offsetof(ohtable, items_per_block_ct_m_prime) == ohtable_items_per_block_ct_m_prime_offset_jazz());
    CHECK(offsetof(ohtable, items_per_block_ct_shift1) == ohtable_items_per_block_ct_shift1_offset_jazz());
    CHECK(offsetof(ohtable, items_per_block_ct_shift2) == ohtable_items_per_block_ct_shift2_offset_jazz());
    CHECK(sizeof(ohtable) == ohtable_sizeof_jazz());
}

typedef struct
{
    u64 block_id;
    size_t index;
} location;

/**
 * @brief Computes the table location for a key using 64-bit FNV-1a
 *        (see: http://isthe.com/chongo/tech/comp/fnv/)
 *
 * @param ohtable
 * @param key      64-bit key
 *
 * @return The computed {block_id, index} location in the table
 */
extern location ohtable_hash_jazz(const ohtable *ohtable, const u64 *key);

static size_t ohtable_hash_from_location(const ohtable *ohtable, location loc)
{
    return loc.block_id * ohtable->items_per_block + loc.index;
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

    CHECK(result->swap_buf = calloc(record_size_qwords, sizeof(*(result->swap_buf))));
    CHECK(result->temp_record = calloc(record_size_qwords, sizeof(*(result->temp_record))));
    
    return result;
}

ohtable *ohtable_create(size_t record_size_qwords, entropy_func getentropy)
{
    #ifdef IS_TEST
    oram *oram = oram_create_depth16(getentropy); 
    #else
    oram *oram = oram_create_120G_16shards(getentropy); 
    #endif
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

    location loc = ohtable_hash_jazz(ohtable, &record[0]);
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
        RETURN_IF_ERROR(oram_function_access_put(ohtable->oram, block_id, &args));
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

error_t ohtable_get(const ohtable *ohtable, u64 key, u64 record[])
{
    // Compute the worst case number of blocks that must be scanned in order to find a record
    // and scan that many blocks for every request. We know the maximum offset of a record from
    // its hash, `ohtable->max_offset`. If the hash is the last record in a block then: (1) if 
    // max_offset is 0 then we only scan one block. (2) if 1 <= max_offset && max_offset <= items_per_block
    // we scan two blocks, etc.
    size_t blocks_to_scan = 1 + (ohtable->max_offset + ohtable->items_per_block-1)/ohtable->items_per_block;
    location loc = ohtable_hash_jazz(ohtable, &key);
    u64 block_id = loc.block_id;
    get_record_accessor_args args = {
        .key = key, 
        .start_index = loc.index, 
        .search_complete = false, 
        .out_record = record, 
        .record_size_qwords = ohtable->record_size_qwords};
        
    while(blocks_to_scan > 0) {
        RETURN_IF_ERROR(oram_function_access_get(ohtable->oram, block_id, &args));
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

/**
 * @brief Jasmin function definitions for OHTable util
 */
extern u64 raw_hash_jazz(u64 *key);
extern void robinhood_accessor_jazz(u64* block_data, void* vargs);
extern void get_record_accessor_jazz(u64* block_data, void* vargs);

static location hash(u64 key, size_t capacity, size_t items_per_block, 
        u64 capacity_ct_m_prime, size_t capacity_ct_shift1, size_t capacity_ct_shift2,
        u64 items_per_block_ct_m_prime, size_t items_per_block_ct_shift1, size_t items_per_block_ct_shift2)
{
    size_t h = ct_mod(raw_hash_jazz(&key), capacity, capacity_ct_m_prime, capacity_ct_shift1, capacity_ct_shift2);

    u64 block_id =  ct_div(h, items_per_block, items_per_block_ct_m_prime, items_per_block_ct_shift1, items_per_block_ct_shift2);
    size_t idx = ct_mod(h, items_per_block, items_per_block_ct_m_prime, items_per_block_ct_shift1, items_per_block_ct_shift2);
    location result = {.block_id = block_id, .index = idx};
    return result;
}

u64 find_collision_for_hash(u64 h, size_t table_capacity, bool new_item_wins) {
    u64 candidate_key;
    u64 candidate_hash;
    bool tie_breaker = false;
    bool found_match = false;
    
    while(!found_match) {
        getentropy(&candidate_key, sizeof(candidate_key));
        candidate_hash = raw_hash_jazz(&candidate_key);
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
        u64 curr_record_hash = raw_hash_jazz(&temp_record[0]) % capacity;
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

        robinhood_accessor_jazz(block_data, &args);
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
        u64 new_key = find_collision_for_hash(raw_hash_jazz(&keys[records_per_block - 1]), capacity, true);

        memset(temp_record, 0, record_size_qwords * sizeof(*temp_record));
        temp_record[0] = new_key;
        u64 curr_record_hash = raw_hash_jazz(&temp_record[0]) % capacity;
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
        robinhood_accessor_jazz(block_data, &args);

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
        u64 curr_record_hash = raw_hash_jazz(&temp_record[0]) % capacity;
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

        robinhood_accessor_jazz(block_data, &args);
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
    u64 new_key = find_collision_for_hash(raw_hash_jazz(&keys[0]), capacity, true);
    {
        TEST_LOG("new_key hash: %" PRIu64 " old key hash: %" PRIu64 "", raw_hash_jazz(&new_key)%capacity, raw_hash_jazz(&keys[0])%capacity);
        memset(temp_record, 0, record_size_qwords * sizeof(*temp_record));
        temp_record[0] = new_key;
        u64 curr_record_hash = raw_hash_jazz(&temp_record[0]) % capacity;
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

        robinhood_accessor_jazz(block_data, &args);
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
    ohtable *table = ohtable_create(RECORD_SIZE_QWORDS, getentropy);
    TEST_LOG("table created with capacity %zu", ohtable_capacity(table));
    TEST_LOG("items per block %zu", table->items_per_block);

    // find a key that maps into the last block
    u64 keys[NUM_COLLISIONS];

    keys[0] = 17096433176034417753UL; // hash: 1956828224092864509
    keys[1] = 1827213764493027053UL; // hash: 2317710734221049853
    keys[2] = 12456615753130599790UL; // hash: 14644380304657219581
    keys[3] = 12704324311469670415UL; // hash: 18235966346540187645
    keys[4] = 8618038463166869607UL; // hash: 18285371504088219645
    keys[5] = 6847023387629127200UL; // hash: 18326111335125614589


    // generate collisions that will force swapping
    for(size_t i = 0; i < NUM_COLLISIONS; ++i) {
        TEST_LOG("Step  %" PRIu64 ": Inserting key: %" PRIu64 " hash: %" PRIu64 "", i, keys[i], raw_hash_jazz(&keys[i]));
        u64 record[RECORD_SIZE_QWORDS];
        // u64 getrecord[RECORD_SIZE_QWORDS];
        memset(record, 0, sizeof(record));
        record[0] = keys[i];
        record[1] = i;
        RETURN_IF_ERROR(ohtable_put(table, record));
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

void private_ohtable_tests()
{
    RUN_TEST(test_rh_accessor_placement());
    RUN_TEST(test_multiswap());
    RUN_TEST(test_wraparound());
}
#endif
