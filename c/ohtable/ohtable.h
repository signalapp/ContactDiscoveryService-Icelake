// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef LIBORAM_OHTABLE_H
#define LIBORAM_OHTABLE_H 1

#include "util/util.h"
#include "util/statistics.h"

typedef struct ohtable ohtable;

/**
 * @brief Uses available memory to create an ORAM-backed hashtable (Oblivious Hashtable - `ohtable`)
 * that stores fixed-size records.
 * 
 * @param record_size_qwords Size of each record, in 64-bit integers.
 * @param available_bytes Bytes available to build this ORAM.
 * @param load_factor Ratio of number of ORAM blocks to number of bucket store leaves.
 *     Between 1.0 and 3.0 inclusive.
 * @param stash_overflow_size Size, in `block`s, of the overflow stash for this ORAM. 
 * @param getentropy entropy function used by internal ORAM for randomness.
 * @return ohtable* Opaque pointer to an `ohtable` object.
 */
ohtable* ohtable_create_for_available_mem(size_t record_size_qwords, size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Creates an ORAM-backed hashtable (Oblivious Hashtable - `ohtable`)
 * that stores fixed-size records.
 *
 * @param record_capacity Minimum number of records the hashtable must hold.
 * @param record_size_qwords Size of each record, in 64-bit integers.
 * @param stash_overflow_size Size of the overflow stash, in `block`s, for the ORAM backing this table.
 * @param getentropy entropy function used by internal ORAM for randomness.
 * @return ohtable* Opaque pointer to an `ohtable` object.
 */
ohtable *ohtable_create(size_t record_capacity, size_t record_size_qwords, size_t stash_overflow_size, entropy_func getentropy);

/**
 * @brief Frees all of the resources used by an `ohtable`
 *
 * @param ohtable
 */
void ohtable_destroy(ohtable *ohtable);

/**
 * @brief Clear all entries from an `ohtable`
 *
 * @param ohtable
 */
void ohtable_clear(ohtable *ohtable);

/**
 * @brief Largest number of items this table can hold
 * 
 * @param ohtable 
 * @return size_t 
 */
size_t ohtable_capacity(const ohtable* ohtable);

/**
 * @brief Number of items stored in table
 * 
 * @param ohtable 
 * @return size_t 
 */
size_t ohtable_num_items(const ohtable* ohtable);
/**
 * @brief The mean displacement of entries from their hash in a table.
 *
 * @param ohtable
 * @return double
 */
double ohtable_mean_displacement(const ohtable *ohtable);

/**
 * @brief Put a record into the hashtable
 *
 * @param ohtable Insert the record into this table.
 * @param record Record to insert. `record[0]` is the key.Must have length matching
 *  `record_size_qwords` used to create the table.
 * @return error_t Error from `oram_put` or `oram_get`
 */
error_t ohtable_put(ohtable *ohtable, const u64 record[]);

/**
 * @brief Retrieve an item from the table
 *
 * @param ohtable Get record from this table.
 * @param key Key for the record to read.
 * @param record Buffer where the record data will be written. Will be identitcally
 * UINT64_MAX if no record is found.
 * @return error_t Error from `oram_get` 
 **/
error_t ohtable_get(const ohtable *ohtable, u64 key, u64 record[]);

// Interface note: when the table statistics are flat structs it would be simpler to return them
// by value. However the ORAM structures backing this table are recursive and some detailed reporting we
// may want in the future will require arrays with sizes determined at runtime. These will require
// nested structures and the _create/_destroy idiom will be helpful.
/**
 * @brief Collect health statistics about this table.
 * 
 * @param ohtable 
 * @return ohtable_statistics* Must be destroyed with `ohtable_statistics_destroy`.
 */
ohtable_statistics* ohtable_statistics_create(ohtable* ohtable);

/**
 * @brief Free all resources in a statistics collection.
 * 
 * @param stats 
 */
void ohtable_statistics_destroy(ohtable_statistics* stats);

#ifdef IS_TEST
void private_ohtable_tests();
int ohtable_trace_get(const ohtable *ohtable, u64 key, u64 record[]);
#endif // IS_TEST

#endif // LIBORAM_OHTABLE_H
