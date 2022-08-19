// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef LIBORAM_SHARDED_OHTABLE_H
#define LIBORAM_SHARDED_OHTABLE_H 1

#include "util/util.h"
#include "util/statistics.h"

typedef struct sharded_ohtable sharded_ohtable;

/**
 * @brief Uses available memory to create sharded oblivious hashtable.
 * 
 * @param record_size_qwords Size of each record, in 64-bit integers.
 * @param num_shards Number of shards to create.
 * @param hash_key secret used to randomize distribution of records to shards.
 * @param available_bytes Bytes available to build this ORAM.
 * @param load_factor Ratio of number of ORAM blocks to number of bucket store leaves.
 *     Between 1.0 and 3.0 inclusive.
 * @param stash_overflow_size ize, in `block`s, of the overflow stash for this ORAM. 
 * @param getentropy entropy function used by internal ORAMs for randomness.
 * @return ohtable* Opaque pointer to an `ohtable` object.
 */
sharded_ohtable* sharded_ohtable_create_for_available_mem(size_t record_size_qwords, size_t num_shards, u8 hash_key[static 8], size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Create a sharded oblivious hashtable.
 *
 * @param record_size_qwords The size of a record, measured in 64-bit words.
 * @param record_capacity The table must have capacity for at least this many records.
 * @param num_shards Number of shards to create.
 * @param hash_key secret used to randomize distribution of records to shards.
 * @param stash_overflow_size Size, in `block`s, of the overflow stash for ORAMs backing the `shard`s' tables.
 * @param getentropy entropy function used by internal ORAMs for randomness.
 * @return sharded_ohtable*
 */
sharded_ohtable *sharded_ohtable_create(size_t record_size_qwords, size_t record_capacity, size_t num_shards, u8 hash_key[static 8], size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Destroy a sharded hashtable and all of the resources it has allocated.
 *
 * @param sharded_ohtable
 */
void sharded_ohtable_destroy(sharded_ohtable *sharded_ohtable);

/**
 * @brief Clear all data from the table.  Blocks.
 *
 * @param table
 */
void sharded_ohtable_clear(sharded_ohtable *table);

/**
 * @brief Runs a shard of the table. Should be run in a separate thread.
 *
 * This runs an infinite loop reading requests, resolving them, and writing responses.
 * Clients need to run the shards in separate threads at startup.
 *
 * @param table Table with shard to start.
 * @param shard_id ID of shard to start
 */
void sharded_ohtable_run_shard(sharded_ohtable *table, size_t shard_id);

/**
 * @brief Signal a shard to stop running and return.
 *
 * The shard runs an infinite loop until signalled to stop. This must be called before
 * the shard's thread can be joined.
 *
 * @param table Table with shard to stop.
 * @param shard_id ID of shard to stop.
 */
void sharded_ohtable_stop_shard(sharded_ohtable *table, size_t shard_id);

/**
 * @brief Put multiple records into the table.
 *
 * @param table Insert the record into this table.
 * @param num_inserts Number of records to insert.
 * @param records Records to insert, laid out in an array of `num_inserts * record_size_qwords` `u64`s.
 * @return err_SUCCESS if successful
 */
error_t sharded_ohtable_put_batch(sharded_ohtable *table, size_t num_inserts, const u64 records[]);

/**
 * @brief Retrieve multiple items from the table.
 *
 * @param table Get records from this table.
 * @param num_queries Number of record keys being passed.
 * @param keys Keys of the records being requested.
 * @param results Results will be written in this array, which must have size at least `num_queries * record_size_qwords`.
 *    If a record is found, it will appear somewhere in the results, but not in any particular order.
 *    For every record that is not found, an "empty record" of `UINT64_MAX` will be written somewhere
 *    in the results.
 * @return err_SUCCESS if successful
 */
error_t sharded_ohtable_get_batch(const sharded_ohtable *table, size_t num_queries, u64 keys[num_queries], u64 results[]);

/**
 * @brief Collect health statistics for each shard's table. 
 * 
 * @param table 
 * @return ohtable_statistics** `num_shards` items. Each must be destroyed with `ohtable_statistics_destroy`.
 */
ohtable_statistics** sharded_ohtable_report_statistics(sharded_ohtable* table);

#endif // LIBORAM_SHARDED_OHTABLE_H
