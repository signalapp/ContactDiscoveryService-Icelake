// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdlib.h>
#include <string.h>
#include "shard.h"
#include "queue.h"
#include "ohtable/ohtable.h"
#include "sharded_ohtable.h"
#include "util/util.h"
#include <halfsiphash.h>
#include <unistd.h>

typedef struct
{
    size_t batch_size;
    size_t num_shards;
    u64 *queries;
    sharded_ohtable_request** requests;
    u64 *responses;
    size_t idx;
    size_t *counters;
} shard_batched_queries;

struct sharded_ohtable
{
    size_t num_shards;
    size_t record_size_qwords;
    shard **shards;
    u8 hash_key[8];
};

static void batched_destroy(shard_batched_queries* batched)
{
    if (batched->queries) free(batched->queries);
    if (batched->requests) {
        for (size_t sh = 0; sh < batched->num_shards; ++sh)
        {
            shard_request_destroy(batched->requests[sh]);
        }
        free(batched->requests);
    }
    if (batched->responses) free(batched->responses);
    if (batched->counters) free(batched->counters);
}

// From https://github.com/signalapp/HsmEnclave/blob/main/hsmc/fixedmap.c
static inline u64 sharded_ohtable_khash(const sharded_ohtable *t, const unsigned char *k)
{
    u64 out = 0;
    halfsiphash(k, sizeof(u64), t->hash_key, (unsigned char *)&out, sizeof(out));
    return out;
}

#define REQUEST_QUERY_BYTES 8
#define REQUEST_INSERT_BYTES 56
#define RESPONSE_QUERY_BYTES 56
#define RESPONSE_INSERT_BYTES 0
#define U64(bytes) ((bytes) / 8)

shard_batched_queries batch_queries(const sharded_ohtable *table, size_t num_queries, const u64 queries[], sharded_ohtable_request_type type)
{
    // 1 u64 for a query, 7 for a record insert
    size_t query_size_bytes = 0;
    size_t response_size_bytes = 0;
    switch (type) {
      case shard_request_query:
        query_size_bytes = REQUEST_QUERY_BYTES;
        response_size_bytes = RESPONSE_QUERY_BYTES;
        break;
      case shard_request_insert:
        query_size_bytes = REQUEST_INSERT_BYTES;
        response_size_bytes = RESPONSE_INSERT_BYTES;
        break;
      default:
        CHECK(false);
    }
    size_t query_size_u64 = U64(query_size_bytes);
    size_t response_size_u64 = U64(response_size_bytes);

    size_t *batch_sizes;
    size_t num_shards = table->num_shards;
    CHECK(batch_sizes = calloc(num_shards, sizeof(*batch_sizes)));
    shard_batched_queries result;
    memset(&result, 0, sizeof(result));
    result.num_shards = num_shards;

    for (size_t i = 0; i < num_queries; ++i)
    {
        for (size_t sh = 0; sh < num_shards; ++sh)
        {
            u64 hashed_key = sharded_ohtable_khash(table, (u8 *)&queries[i * query_size_u64]);
            batch_sizes[sh] += U64_TERNARY(shard_contains(table->shards[sh], hashed_key), 1, 0);
        }
    }
    for (size_t sh = 0; sh < num_shards; ++sh)
    {
      result.batch_size = U64_TERNARY(result.batch_size < batch_sizes[sh], batch_sizes[sh], result.batch_size);
    }

    free(batch_sizes);

    // we know the max batch size so we can allocate arrays
    size_t total_queries = num_shards * result.batch_size;

    CHECK(result.responses = calloc(total_queries, response_size_bytes));
    CHECK(result.requests = calloc(num_shards, sizeof(result.requests[0])));
    CHECK(result.counters = calloc(num_shards, sizeof(result.counters[0])));
    // We allocate one additional query record, since if the last shard is
    // full, queries after it may do useless work in result.queries[total_queries].
    CHECK(result.queries = calloc(total_queries+1, query_size_bytes));

    for (size_t i = 0; i < num_queries; ++i)
    {
        const u64* orig_query = &queries[i*query_size_u64];
        u64 hashed_key = sharded_ohtable_khash(table, (u8 *)orig_query);
        for (size_t sh = 0; sh < num_shards; ++sh)
        {
            // Queries are laid out contiguously per shard.  For example, with 2 shards
            // and batch_size=4, we'd have (sh=shard,q=query):
            //   result.queries = [sh0q0,sh0q1,sh0q2,empty,sh1q0,sh1q1,sh1q2,sh1q3]
            size_t new_query_idx = sh * result.batch_size + result.counters[sh];
            u64* new_query = &result.queries[new_query_idx * query_size_u64];

            bool contained = shard_contains(table->shards[sh], hashed_key);
            result.counters[sh] += U64_TERNARY(contained, 1, 0);

            // Conditional, constant-time memcpy
            for (size_t q = 0; q < query_size_u64; ++q)
            {
                new_query[q] = U64_TERNARY(contained, orig_query[q], new_query[q]);
            }
        }
    }

    size_t idx = 0;
    for (size_t sh = 0; sh < num_shards; ++sh, ++idx)
    {
        shard* shard = table->shards[sh];
        size_t idx = result.batch_size * sh;
        switch (type) {
          case shard_request_insert:
            result.requests[sh] = shard_insert(
                shard,
                result.queries + idx * query_size_u64,
                result.batch_size);
            break;
          case shard_request_query:
            result.requests[sh] = shard_query(
                shard,
                result.queries + idx * query_size_u64,
                result.responses + idx * response_size_u64,
                result.batch_size);
            break;
          default:
            CHECK(false);
        }
    }

    for (size_t sh = 0; sh < num_shards; sh++) {
      shard_wait(table->shards[sh]);
    }

    return result;
}

sharded_ohtable* _create(size_t record_size_qwords, size_t num_shards, u8 hash_key[static 8]) {
    shard **shards;
    CHECK(shards = calloc(num_shards, sizeof(*shards)));
    
    sharded_ohtable *result;
    CHECK(result = calloc(1, sizeof(*result)));
    result->num_shards = num_shards;
    result->shards = shards;
    result->record_size_qwords = record_size_qwords;
    memcpy(result->hash_key, hash_key, 8);
    return result;

}

sharded_ohtable* sharded_ohtable_create_for_available_mem(size_t record_size_qwords, size_t num_shards, u8 hash_key[static 8], size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy) {

   
    sharded_ohtable* result = _create(record_size_qwords, num_shards, hash_key);
    u64 step_size = UINT64_MAX / num_shards;

    size_t per_shard_available_bytes = (available_bytes - sizeof(*result)) / num_shards;

    for (size_t i = 0; i < num_shards; ++i)
    {
        u64 lb = i * step_size;
        u64 ub = (i == num_shards - 1) ? UINT64_MAX : (i + 1) * step_size;
        result->shards[i] = shard_create_for_available_mem(lb, ub, record_size_qwords, per_shard_available_bytes, load_factor, stash_overflow_size, getentropy);
    }
    
    return result;
}

sharded_ohtable *sharded_ohtable_create(size_t record_size_qwords, size_t record_capacity, size_t num_shards, u8 hash_key[static 8], size_t stash_overflow_size, entropy_func getentropy)
{
   
    sharded_ohtable* result = _create(record_size_qwords, num_shards, hash_key);
    u64 step_size = UINT64_MAX / num_shards;

    size_t shard_capacity = 1 + record_capacity / num_shards;
    for (size_t i = 0; i < num_shards; ++i)
    {
        u64 lb = i * step_size;
        u64 ub = (i == num_shards - 1) ? UINT64_MAX : (i + 1) * step_size;
        result->shards[i] = shard_create(lb, ub, record_size_qwords, shard_capacity, stash_overflow_size, getentropy);
    }
    
    return result;
}

void sharded_ohtable_destroy(sharded_ohtable *sharded_ohtable)
{
    if (sharded_ohtable)
    {
        for (size_t i = 0; i < sharded_ohtable->num_shards; ++i)
        {
            shard_destroy(sharded_ohtable->shards[i]);
        }
        free(sharded_ohtable->shards);
        free(sharded_ohtable);
    }
}

void sharded_ohtable_clear(sharded_ohtable *table)
{
    for (size_t i = 0; i < table->num_shards; ++i)
    {
        sharded_ohtable_request* req = shard_clear(table->shards[i]);
        shard_wait(table->shards[i]);
        shard_request_destroy(req);
    }
}

void sharded_ohtable_run_shard(sharded_ohtable *table, size_t shard_id)
{
    CHECK(shard_id < table->num_shards);
    shard_run(table->shards[shard_id]);
}

void sharded_ohtable_stop_shard(sharded_ohtable *table, size_t shard_id)
{
    TEST_LOG("Stopping shard %ld", shard_id);
    CHECK(shard_id < table->num_shards);
    shard_stop(table->shards[shard_id]);
}

error_t sharded_ohtable_put_batch(
    sharded_ohtable *table,
    size_t num_inserts,
    const u64 records[])
{
    shard_batched_queries batches = batch_queries(table, num_inserts, records, shard_request_insert);
    error_t err = err_SUCCESS;
    for (size_t sh = 0; sh < batches.num_shards; sh++) {
      sharded_ohtable_request* req = batches.requests[sh];
      if (err_SUCCESS != (err = shard_request_error(req))) break;
    }
    batched_destroy(&batches);
    return err;
}

error_t sharded_ohtable_get_batch(
    const sharded_ohtable *table,
    size_t num_queries,
    u64 keys[num_queries],
    u64 results[])
{
    shard_batched_queries batches = batch_queries(table, num_queries, keys, shard_request_query);
    error_t err = err_SUCCESS;
    size_t idx = 0;
    memset(results, 0xff, RESPONSE_QUERY_BYTES * num_queries);
    for (size_t sh = 0; sh < batches.num_shards; sh++) {
      sharded_ohtable_request* req = batches.requests[sh];
      if (err_SUCCESS != (err = shard_request_error(req))) break;
      u64* response = shard_request_response(req);
      for (size_t i = 0; i < batches.counters[sh]; i++) {
        u64* response_i = response + i*U64(RESPONSE_QUERY_BYTES);
        u64* result = results + idx*U64(RESPONSE_QUERY_BYTES);
        if (response_i[0] != UINT64_MAX) {
          memcpy(result, response_i, RESPONSE_QUERY_BYTES);
          idx++;
        }
      }
    }
    batched_destroy(&batches);
    return err;
}


ohtable_statistics** sharded_ohtable_report_statistics(sharded_ohtable* table) {
    ohtable_statistics** stats;
    CHECK(stats = calloc(table->num_shards, sizeof(*stats)));
    for(size_t i = 0; i < table->num_shards; ++i) {
        stats[i] = shard_report_ohtable_statisitics(table->shards[i]);
    }

    return stats;
}

void sharded_ohtable_statistics_destroy(const sharded_ohtable* table, ohtable_statistics** stats) {
    if(stats) {
        for(size_t i = 0; i < table->num_shards; ++i) {
            ohtable_statistics_destroy(stats[i]);
        }
        free(stats);
    }
}
