// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "shard.h"
#include "queue.h"
#include "ohtable/ohtable.h"
#include "sharded_ohtable.h"
#include "util/util.h"
#include <halfsiphash.h>

// Record layout constants used across C and Jasmin paths
#define RECORD_SIZE_BYTES 56
#define KEY_SIZE 8
#define U64(bytes) ((bytes) / 8)

// Jasmin constant export for NUM_SHARDS
extern size_t sharded_ohtable_num_shards_const_jazz(void);

// struct batchable_request layout must match Jasmin definitions in sharded_ohtable.jinc.
// Jasmin is the source of truth; validated at runtime by batchable_request_validate_layout().
typedef struct
{
    u64 shard_id;
    u64 flag;
    u64 found;
    u64 client_id;
    u64 orig_position;
    u64 record[0];
} batchable_request;

// Jasmin-exported layout functions for batchable_request
extern size_t batchable_request_shard_id_offset_jazz(void);
extern size_t batchable_request_flag_offset_jazz(void);
extern size_t batchable_request_found_offset_jazz(void);
extern size_t batchable_request_client_id_offset_jazz(void);
extern size_t batchable_request_orig_position_offset_jazz(void);
extern size_t batchable_request_record_offset_jazz(void);
extern size_t batchable_request_block_size_jazz(void);

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void batchable_request_validate_layout(void) {
    CHECK(offsetof(batchable_request, shard_id) == batchable_request_shard_id_offset_jazz());
    CHECK(offsetof(batchable_request, flag) == batchable_request_flag_offset_jazz());
    CHECK(offsetof(batchable_request, found) == batchable_request_found_offset_jazz());
    CHECK(offsetof(batchable_request, client_id) == batchable_request_client_id_offset_jazz());
    CHECK(offsetof(batchable_request, orig_position) == batchable_request_orig_position_offset_jazz());
    CHECK(offsetof(batchable_request, record) == batchable_request_record_offset_jazz());
    // Validate the Jasmin-computed block size matches our C layout with fixed record size.
    // The batchable_request block consists of the header plus one record per request.
    CHECK(batchable_request_block_size_jazz() == (sizeof(batchable_request) + RECORD_SIZE_BYTES));
}

// struct shard_batched_queries layout must match Jasmin definitions in sharded_ohtable.jinc.
// Jasmin is the source of truth; validated at runtime by shard_batched_queries_validate_layout().
typedef struct
{
    size_t batch_size;
    size_t num_shards;
    size_t num_client_requests;
    u64 *records;
    sharded_ohtable_request** shard_requests;
    batchable_request *client_requests;
} shard_batched_queries;

// Jasmin-exported layout functions for shard_batched_queries
extern size_t shard_batched_queries_sizeof_jazz(void);
extern size_t shard_batched_queries_batch_size_offset_jazz(void);
extern size_t shard_batched_queries_num_shards_offset_jazz(void);
extern size_t shard_batched_queries_num_client_requests_offset_jazz(void);
extern size_t shard_batched_queries_records_offset_jazz(void);
extern size_t shard_batched_queries_shard_requests_offset_jazz(void);
extern size_t shard_batched_queries_client_requests_offset_jazz(void);

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void shard_batched_queries_validate_layout(void) {
    CHECK(offsetof(shard_batched_queries, batch_size) == shard_batched_queries_batch_size_offset_jazz());
    CHECK(offsetof(shard_batched_queries, num_shards) == shard_batched_queries_num_shards_offset_jazz());
    CHECK(offsetof(shard_batched_queries, num_client_requests) == shard_batched_queries_num_client_requests_offset_jazz());
    CHECK(offsetof(shard_batched_queries, records) == shard_batched_queries_records_offset_jazz());
    CHECK(offsetof(shard_batched_queries, shard_requests) == shard_batched_queries_shard_requests_offset_jazz());
    CHECK(offsetof(shard_batched_queries, client_requests) == shard_batched_queries_client_requests_offset_jazz());
    CHECK(sizeof(shard_batched_queries) == shard_batched_queries_sizeof_jazz());
}

// struct sharded_ohtable layout must match Jasmin definitions in sharded_ohtable.jinc.
// Jasmin is the source of truth; validated at runtime by sharded_ohtable_validate_layout().
struct sharded_ohtable
{
    size_t num_shards;
    size_t record_size_qwords;
    shard **shards;
    u8 hash_key[8];
    u64 ct_divisor_step;
    u64 ct_div_m_prime;
    size_t ct_div_shift1;
    size_t ct_div_shift2;
};

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void sharded_ohtable_validate_layout(void) {
    // Validate that the Jasmin-compiled NUM_SHARDS matches runtime num_shards
    CHECK(offsetof(sharded_ohtable, shards) == sharded_ohtable_shards_offset_jazz());
    CHECK(offsetof(sharded_ohtable, hash_key) == sharded_ohtable_hash_key_offset_jazz());
    CHECK(offsetof(sharded_ohtable, ct_divisor_step) == sharded_ohtable_ct_divisor_step_offset_jazz());
    CHECK(offsetof(sharded_ohtable, ct_div_m_prime) == sharded_ohtable_ct_div_m_prime_offset_jazz());
    CHECK(offsetof(sharded_ohtable, ct_div_shift1) == sharded_ohtable_ct_div_shift1_offset_jazz());
    CHECK(offsetof(sharded_ohtable, ct_div_shift2) == sharded_ohtable_ct_div_shift2_offset_jazz());
    CHECK(sizeof(sharded_ohtable) == sharded_ohtable_sizeof_jazz());
}

inline static size_t sizeof_batchable_request(const sharded_ohtable *table) {
  return sizeof(batchable_request) + table->record_size_qwords*sizeof(u64);
}

static void batched_destroy(shard_batched_queries* batched)
{
    if (batched->records) free(batched->records);
    if (batched->shard_requests) {
        for (size_t sh = 0; sh < batched->num_shards; ++sh)
        {
            shard_request_destroy(batched->shard_requests[sh]);
        }
        free(batched->shard_requests);
    }
    if(batched->client_requests) free(batched->client_requests);
}

// Jasmin-exported algorithmic functions
extern size_t calculate_batch_size(const sharded_ohtable *table, size_t num_queries, batchable_request requests[]);

extern void prepare_query_batches_query(
  const sharded_ohtable *table, size_t num_queries, u8 *all_requests, size_t b, shard_batched_queries* result, size_t* distances);

extern void prepare_query_batches_insert(
  const sharded_ohtable *table, size_t num_queries, u8 *all_requests, size_t b, shard_batched_queries* result, size_t* distances);

extern error_t extract_results_from_batch_jazz(shard_batched_queries *sbq, u64 results[], u8 *all_requests, size_t n, size_t *distances);


// Use Snoopy batching strategy (https://eprint.iacr.org/2021/1280.pdf) except for the
// batch size computation.
shard_batched_queries prepare_query_batches(const sharded_ohtable *table, size_t num_queries, batchable_request requests[], sharded_ohtable_request_type type) {
  size_t request_size_u8 = sizeof_batchable_request(table);

  size_t b = calculate_batch_size(table, num_queries, requests);
  size_t n = num_queries + b * table->num_shards;

  // Create result structure
  shard_batched_queries result;
  memset(&result, 0, sizeof(result));
  result.num_shards = table->num_shards;
  result.batch_size = b;
  result.num_client_requests = num_queries;
  size_t record_size_bytes = table->record_size_qwords * 8;

  size_t total_queries = table->num_shards * result.batch_size;

  CHECK(result.shard_requests = calloc(table->num_shards, sizeof(result.shard_requests[0])));

  // We allocate one additional query record, since if the last shard is
  // full, queries after it may do useless work in result.records[total_queries].
  CHECK(result.records = calloc(total_queries+1, record_size_bytes));


  // STEP 1: assign requests to shards
  // create array for real and dummy requests
  u8 *all_requests = 0;
  CHECK(all_requests = calloc(n, request_size_u8));

  // copy the real requests into the array, compute shard_id, and set the flag to be zero
  memcpy(all_requests, requests, num_queries * request_size_u8);

  size_t* distances = 0;
  CHECK(distances = calloc(n, sizeof(distances[0])));
  switch (type) {
    case shard_request_insert:
      prepare_query_batches_insert(table, num_queries, all_requests, b, &result, distances);
      break;
    case shard_request_query:
      prepare_query_batches_query(table, num_queries, all_requests, b, &result, distances);
      break;
    default:
      CHECK(false);
  }
  free(distances);

  for (size_t sh = 0; sh < table->num_shards; ++sh)
  {
      shard* shard = table->shards[sh];
      size_t idx = result.batch_size * sh;
      switch (type) {
        case shard_request_insert:
          result.shard_requests[sh] = shard_insert(
              shard,
              result.records + idx * table->record_size_qwords,
              result.batch_size);
          break;
        case shard_request_query:
          result.shard_requests[sh] = shard_query(
              shard,
              result.records + idx * table->record_size_qwords,
              result.batch_size);
          break;
        default:
          CHECK(false);
      }
  }

  for (size_t sh = 0; sh < table->num_shards; sh++) {
    shard_wait(table->shards[sh]);
  }

  free(all_requests);
  return result;
}

shard_batched_queries batch_queries(const sharded_ohtable *table, size_t num_queries, const u64 queries[], sharded_ohtable_request_type type)
{
  u8 *requests;
  size_t query_size_u64 = type == shard_request_insert ? table->record_size_qwords : 1;
  size_t request_size_u8 = sizeof_batchable_request(table);
  CHECK(requests = calloc(num_queries, request_size_u8));

  for(size_t i = 0; i < num_queries; ++i) {
    batchable_request *req = (batchable_request*)(requests + i * request_size_u8);
    req->orig_position = i;
    memcpy(req->record, queries + i*query_size_u64, query_size_u64 * sizeof(u64));
  }
  shard_batched_queries result = prepare_query_batches(table, num_queries, (batchable_request*)requests, type);
  result.client_requests = (batchable_request*)requests;
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

    u64 step_size = UINT64_MAX / num_shards;
    result->ct_divisor_step = step_size;
    prep_ct_div(step_size, &result->ct_div_m_prime, &result->ct_div_shift1, &result->ct_div_shift2);

    // Ensure Jasmin NUM_SHARDS matches runtime
    CHECK(sharded_ohtable_num_shards_const_jazz() == num_shards);
    return result;
}

sharded_ohtable* sharded_ohtable_create_for_available_mem(size_t record_size_qwords, size_t num_shards, u8 hash_key[static 8], size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy) {


    sharded_ohtable* result = _create(record_size_qwords, num_shards, hash_key);
    u64 step_size = result->ct_divisor_step;

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
    u64 step_size = result->ct_divisor_step;

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
      sharded_ohtable_request* req = batches.shard_requests[sh];
      if (err_SUCCESS != (err = shard_request_error(req))) break;
    }
    batched_destroy(&batches);
    return err;
}

error_t extract_results_from_batch(const sharded_ohtable *table, shard_batched_queries sbq, u64 results[]) {
  error_t err = err_SUCCESS;
  size_t n = sbq.batch_size * sbq.num_shards + sbq.num_client_requests;
  size_t request_size_u8 = sizeof_batchable_request(table);
  u8 *all_requests = 0;
  CHECK(all_requests = calloc(n, request_size_u8));

  // allocate scratch space for ocompact
  size_t *distances = 0;
  CHECK(distances = calloc(n, sizeof(distances[0])));

  err = extract_results_from_batch_jazz(&sbq, results, all_requests, n, distances);

  free(distances);
  free(all_requests);
  return err;
}

error_t sharded_ohtable_get_batch(
    const sharded_ohtable *table,
    size_t num_queries,
    u64 keys[num_queries],
    u64 results[])
{
    shard_batched_queries batches = batch_queries(table, num_queries, keys, shard_request_query);
    error_t err = extract_results_from_batch(table, batches, results);

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
