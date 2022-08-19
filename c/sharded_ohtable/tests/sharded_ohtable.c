// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/random.h>

#include "sharded_ohtable/sharded_ohtable.h"
#include "util/util.h"
#include "util/tests.h"
#include "ohtable/ohtable.h"

typedef struct
{
    uint64_t e164;
    uint64_t aci[2];
    uint64_t pni[2];
    uint64_t uak[2];
} record;

typedef struct run_shard_args
{
    sharded_ohtable *table;
    size_t shard_id;
} run_shard_args;

void *shard_thread_func(void *input)
{
    run_shard_args *args = input;
    sharded_ohtable_run_shard(args->table, args->shard_id);
    free(args);
    return 0;
}

#define NUM_SHARDS 15

#define RECORDS_TO_INSERT 100
#define RECORD_SIZE_QWORDS 7

u64 insert_records[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];
u64 queries[RECORDS_TO_INSERT];
u64 notfound_queries[RECORDS_TO_INSERT];

u8 hash_key[8] = {233, 144, 89, 55, 34, 21, 13, 8};

pthread_t start_shard_thread(sharded_ohtable *table, size_t shard_id)
{
    pthread_t tid;
    run_shard_args *args = calloc(1, sizeof(*args));
    args->table = table;
    args->shard_id = shard_id;

    pthread_create(&tid, NULL, shard_thread_func, args);

    return tid;
}

void prepare_queries_and_inserts()
{
    memset(insert_records, 0, sizeof(insert_records));
    memset(queries, 0, sizeof(queries));
    memset(notfound_queries, 0, sizeof(notfound_queries));
    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        uint64_t buf[1];
        getentropy(buf, sizeof(buf));
        u64 id = i == 0 ? 3061075565009377566ull : i; // 3061075565009377566ull hashes to same location as 0 and can cause fun
        insert_records[RECORD_SIZE_QWORDS * i] = id;
        queries[i] = id;
        notfound_queries[i] = id + RECORDS_TO_INSERT;
    }
}

// validate that records[:num] contains all of queries[:num]
bool validate_all_matches(u64* results, u64* queries, size_t num) {
    for (size_t i = 0; i < num; ++i)
    {
        bool found = false;
        for (size_t j = 0; j < num; ++j)
        {
            if (results[j * RECORD_SIZE_QWORDS] == queries[i])
            {
                found = true;
                break;
            }
        }
        if (!found) {
          return false;
        }
    }
    return true;
}

int test_insert_query_load()
{
    prepare_queries_and_inserts();

    TEST_LOG("creating table");
    sharded_ohtable *table = sharded_ohtable_create(RECORD_SIZE_QWORDS, 5000000, NUM_SHARDS, hash_key, TEST_STASH_SIZE, getentropy);
    pthread_t shard_tids[NUM_SHARDS];
    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        shard_tids[i] = start_shard_thread(table, i);
    }

    TEST_LOG("putting batches");
    // everything is up and running so insert some data and issue some queires
    TEST_ERR(sharded_ohtable_put_batch(table, RECORDS_TO_INSERT, insert_records));

    u64 results[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];

    TEST_LOG("getting batches found");
    // Test that all are found
    TEST_ERR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT, queries, results));
    TEST_ASSERT(validate_all_matches(results, queries, RECORDS_TO_INSERT));

    TEST_LOG("getting batches not-found");
    TEST_ERR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT, notfound_queries, results));
    for (int i = 0; i < RECORDS_TO_INSERT; i++) {
      TEST_ASSERT(results[i*RECORD_SIZE_QWORDS] == UINT64_MAX);
    }

    // Test that a subset is found.
    TEST_LOG("getting batches subset");
    TEST_ERR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT / 2, queries, results));
    TEST_ASSERT(validate_all_matches(results, queries, RECORDS_TO_INSERT / 2));

    TEST_LOG("stopping");
    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        sharded_ohtable_stop_shard(table, i);
        pthread_join(shard_tids[i], 0);
    }

    sharded_ohtable_destroy(table);

    return err_SUCCESS;
}

int test_large_load()
{
    prepare_queries_and_inserts();

    size_t record_capacity = 50000;
    size_t num_records_to_add = 25000;
    size_t num_blocks = 20;
    TEST_ASSERT(num_records_to_add % num_blocks == 0);
    size_t records_per_block = num_records_to_add / num_blocks;

    sharded_ohtable *table = sharded_ohtable_create(RECORD_SIZE_QWORDS, record_capacity, NUM_SHARDS, hash_key, TEST_STASH_SIZE, getentropy);
    pthread_t shard_tids[NUM_SHARDS];
    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        shard_tids[i] = start_shard_thread(table, i);
    }

    record *data = calloc(records_per_block, sizeof(*data));
    printf("adding %zu blocks for %zu total records\n", num_blocks, num_records_to_add);

    for (size_t block = 0; block < num_blocks; ++block)
    {
        if (block % (num_blocks / 20) == 0)
        {
            fprintf(stderr, "block %zu/%zu\n", block, num_blocks);
        }
        memset(data, 0, records_per_block * sizeof(*data));
        for (size_t i = 0; i < records_per_block; ++i)
        {
            uint64_t buf[1];
            getentropy(buf, sizeof(buf));
            data[i].e164 = buf[0];
        }
        RETURN_IF_ERROR(sharded_ohtable_put_batch(table, records_per_block, (u64 *)data));
    }
    free(data);

    RETURN_IF_ERROR(sharded_ohtable_put_batch(table, RECORDS_TO_INSERT, insert_records));

    u64 results[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];
    RETURN_IF_ERROR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT, queries, results));

    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        size_t index_of_item_i = UINT64_MAX;
        for (size_t j = 0; j < RECORDS_TO_INSERT; ++j)
        {
            if (results[j * RECORD_SIZE_QWORDS] == queries[i])
            {
                index_of_item_i = j;
                break;
            }
        }
        if (i % 1000 == 0)
        {
            printf("found %" PRIu64 " query number %zu in position %zu\n", queries[i], i, index_of_item_i);
        }
        if(index_of_item_i == UINT64_MAX) {
            TEST_LOG("  Failed to retrieve item %zu (id: %" PRIu64 ")", i, queries[i]);
        }

        TEST_ASSERT(index_of_item_i < UINT64_MAX);
    }

    // now clear the table
    sharded_ohtable_clear(table);

    // and make sure the records aren't there anymore
    memset(results, 0, RECORD_SIZE_QWORDS * RECORDS_TO_INSERT * 8);
    RETURN_IF_ERROR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT, queries, results));

    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        TEST_ASSERT(results[i * RECORD_SIZE_QWORDS] == UINT64_MAX);
    }

    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        sharded_ohtable_stop_shard(table, i);
        pthread_join(shard_tids[i], 0);
    }

    sharded_ohtable_destroy(table);

    return err_SUCCESS;
}


int test_create_for_mem()
{
    prepare_queries_and_inserts();

    size_t avail_mem = (1ul << 29);

    // create a table for the memory
    sharded_ohtable *table = sharded_ohtable_create_for_available_mem(RECORD_SIZE_QWORDS, NUM_SHARDS, hash_key, avail_mem, 1.6, TEST_STASH_SIZE, getentropy);
    
    // make sure it works
    pthread_t shard_tids[NUM_SHARDS];
    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        shard_tids[i] = start_shard_thread(table, i);
    }

    // everything is up and running so insert some data and issue some queires
    TEST_ERR(sharded_ohtable_put_batch(table, RECORDS_TO_INSERT, insert_records));

    u64 results[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];

    // Test that all are found
    TEST_ERR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT, queries, results));
    TEST_ASSERT(validate_all_matches(results, queries, RECORDS_TO_INSERT));

    TEST_ERR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT, notfound_queries, results));
    for (int i = 0; i < RECORDS_TO_INSERT; i++) {
      TEST_ASSERT(results[i*RECORD_SIZE_QWORDS] == UINT64_MAX);
    }

    // Test that a subset is found.
    TEST_ERR(sharded_ohtable_get_batch(table, RECORDS_TO_INSERT / 2, queries, results));
    TEST_ASSERT(validate_all_matches(results, queries, RECORDS_TO_INSERT / 2));

    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        sharded_ohtable_stop_shard(table, i);
        pthread_join(shard_tids[i], 0);
    }

    // get statistics
    ohtable_statistics** stats = sharded_ohtable_report_statistics(table);

    sharded_ohtable_destroy(table);
    for(size_t i = 0; i < NUM_SHARDS; ++i) {
        ohtable_statistics_destroy(stats[i]);
    }
    free(stats);

    return err_SUCCESS;
}

int main()
{
    RUN_TEST(test_insert_query_load());
    RUN_TEST(test_large_load());
    RUN_TEST(test_create_for_mem());
    return 0;
}
