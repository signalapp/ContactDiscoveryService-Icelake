// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef LIBORAM_SHARDED_OHTABLE_SHARD_H
#define LIBORAM_SHARDED_OHTABLE_SHARD_H 1

#include <stdbool.h>
#include "util/util.h"
#include "util/statistics.h"
#include "sharded_ohtable/queue.h"

/**
 * @brief The `shard` is a component used by the `sharded_ohtable` and is
 * not designed to be used directly.
 *
 */

typedef struct shard shard;

typedef enum
{
    shard_request_unknown,
    shard_request_insert,
    shard_request_query,
    shard_request_stop,
    shard_request_clear,
    shard_request_wait,
} sharded_ohtable_request_type;

typedef struct sharded_ohtable_request sharded_ohtable_request;


/**
 * @brief Uses available memory to create an ORAM-backed hashtable shard.  Used internally by `sharded_ohtable`.
 * 
 * @param lb Lower bound for keys stored in this shard.
 * @param ub Upper bound for keys stored in this shard.
 * @param record_size_qwords Size of each record, in 64-bit integers.
 * @param available_bytes Bytes available to build this ORAM.
 * @param load_factor Ratio of number of ORAM blocks to number of bucket store leaves.
 *     Between 1.0 and 3.0 inclusive.
 * @param stash_overflow_size ize, in `block`s, of the overflow stash for this ORAM. 
 * @param getentropy entropy function used by internal ORAM for randomness.
 * @return shard*
 */
shard* shard_create_for_available_mem(u64 lb, u64 ub, size_t record_size_qwords, size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Used internally by `sharded_ohtable`.
 * Create a table shard with a specified capacity and a range of values to be managed.
 *
 * @param lb Lower bound for keys stored in this shard.
 * @param ub Upper bound for keys stored in this shard.
 * @param record_size_qwords Size of a data record in 64-bit words.
 * @param record_capacity Number of records this shard must be able to hold.
 * @param stash_overflow_size Size, in `block`s, of the overflow stash for ORAMs backing this `shard`'s table
 * @param getentropy entropy function used by internal ORAM for randomness.
 * @return shard*
 */
shard* shard_create(u64 lb, u64 ub, size_t record_size_qwords, size_t record_capacity, size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Release all resources owned by this shard
 *
 * @param shard
 */
void shard_destroy(shard *shard);

/**
 * @brief Clear all data in a shard's table.
 *
 * @param shard
 */
sharded_ohtable_request* shard_clear(shard *shard);

/**
 * @brief Determines if a shard manages this key.
 *
 * @param shard
 * @param key
 * @return true
 * @return false
 */
bool shard_contains(const shard *shard, u64 key);

/**
 * @brief Add a record to a shard.
 *
 * @param shard shard to hold record
 * @param record record to insert.  Must live longer than the request.
 * @return async request.  Call shard_wait() before using.
 */
sharded_ohtable_request* shard_insert(shard *shard, const u64 *record, size_t num_records);

/**
 * @brief Check the table to see if a record is present for a given key.
 *
 * @param shard shard to query
 * @param key key to lookup.  Must live longer than the request.
 * @param response where to put the response for this query (must match
 *        backing ohtable's record size).  Must live longer than the request.
 * @return async request.  Call shard_wait() before using.
 */
sharded_ohtable_request* shard_query(shard *shard, const u64 *key, u64* response, size_t num_queries);

/**
 * @brief Wait synchronously for all previously submitted requests to complete.
 */
void shard_wait(shard* shard);

/**
 * @brief Free resources allocated for a response.
 *
 * @param r
 */
void shard_request_destroy(sharded_ohtable_request *r);

/** @brief Get the error out of a finished request.
 *
 * Must call shard_wait before this.
 *
 * @param r
 */
error_t shard_request_error(sharded_ohtable_request* r);

/** @brief Get the response out of a finished request.
 *
 * Must call shard_wait before this.
 *
 * @param r
 */
u64* shard_request_response(sharded_ohtable_request* r);

/**
 * @brief  Listens to a request queue for inserts and queries and processses them.
 * Run by shard worker thread.
 *
 * @param shard
 */
void shard_run(shard *shard);

/**
 * @brief Signals a running shard to stop. Shard will process and respond to all previous queires
 * before stopping.
 *
 * Blocks until the shard has stopped.
 *
 * @param shard
 */
void shard_stop(shard *shard);
/**
 * @brief Collect health statistics about this shard's table.
 * 
 * @param shard 
 * @return ohtable_statistics* Must be destroyed with `ohtable_statistics_destroy`.
 */
ohtable_statistics* shard_report_ohtable_statisitics(shard* shard);

#ifdef IS_TEST
void run_shard_tests();
#endif // IS_TEST

#endif // LIBORAM_SHARDED_OHTABLE_SHARD_H
