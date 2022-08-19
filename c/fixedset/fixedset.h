// Copyright 2022-2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _CDSI_FIXEDMAP_H
#define _CDSI_FIXEDMAP_H

#include <stdbool.h>
#include <stddef.h>
#include "util/util.h"

#define FIXEDMAP_HALFSIPHASH_KEYSIZE 8

/** fixedset_t is a hashset where keys are fixed-sized byte strings.
 *
 * We implement this as a growing-only RobinHood hash with hashing on the bytes.
 * Entries take up 4 + ksize bytes.  Writes should be relatively fast, and reads
 * are optimized but also constant-time (for an underlying, non-mutating set).
 * The table resizes up at ~88% utilization.
 *
 * fixedset_t copies its keys into internal storage:
 *
 *   fixedset_t* m;
 *   uint32_t k = 1;
 *   fixedset_new(&m, sizeof(k));
 *   fixedset_upsert(m, &k, NULL);
 *
 *   // [k] has been copied in, and can be modified locally
 *
 *   k = 2;
 *   bool found = fixedset_get(m, &k);
 *   assert(!found);
 *   
 *   k = 1;
 *   fixedset_get(m, &k, &found);
 *   assert(found);
 */
struct fixedset_t;
typedef struct fixedset_t fixedset_t;

/** Creates a new fixedset.
 * \memberof fixedset_t
 *
 * Args:
 *  @param *h Output will be written here on successful allocation/creation.
 *  @param ksize Size of each key
 *
 * @return
 *  err_SUCCESS: *h contains a new, usable fixedset.
 *  err: *h unchanged, nothing allocated, error during creation.
 */
error_t fixedset_new(fixedset_t** h, size_t ksize, unsigned char sipkey[FIXEDMAP_HALFSIPHASH_KEYSIZE]);

/** Deallocate a fixedset.
 * \memberof fixedset_t
 *
 * Args:
 *  @param h Object to free/deallocate.
 */
void fixedset_free(fixedset_t* h);

/** Clear all entries from the map.
 * \memberof fixedset_t */
void fixedset_clear(fixedset_t* h);

/** Insert or replace the value at key [k].
 * \memberof fixedset_t
 *
 * Args:
 *  @param h Map to upsert into
 *  @param k Pointer to key (of size ksize)
 *  @param replaced If non-null, set to true or false depending on whether
 *      [k] already had an entry in the map that was replaced with this call
 *
 * @return
 *  err_SUCCESS:  h[k] now in the set.
 *  err:  [h] and [replaced] unchanged
 */
error_t fixedset_upsert(fixedset_t* h, const void* k, bool* replaced);

/** Return whether [k] is in the set.
 * \memberof fixedset_t
 *
 * Args:
 *  @param h Map to read from
 *  @param k Pointer to key (of size ksize)
 *
 * @return
 *  true: [k] was found in the map.
 *  false: [k] was not found in the map.
 */
bool fixedset_get(fixedset_t* h, const void* k);

/** Remove the key [k] from the map.
 * \memberof fixedset_t
 *
 * Args:
 *  @param h Map to remove from
 *  @param k Pointer to key (of size ksize)
 *
 * @return
 *  true: [k]'s entry was found and removed.
 *  false: [k] has no entry in the map.
 */
bool fixedset_remove(fixedset_t* h, const void* k);

/** Return the size (number of set keys) in the given map [h]
 * \memberof fixedset_t */
size_t fixedset_size(fixedset_t* h);

/** Return the capacity (number of spaces) in the given map [h]
 * \memberof fixedset_t */
size_t fixedset_capacity(fixedset_t* h);

#endif  // _CDSI_FIXEDMAP_H
