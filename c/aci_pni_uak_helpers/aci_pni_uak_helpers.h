// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDSI_ENC_ACI_LOOKUP_H
#define CDSI_ENC_ACI_LOOKUP_H 1
#include <stdint.h>
#include <stddef.h>
#include "fixedset/fixedset.h"

typedef struct
{
    uint64_t aci[2];
    uint64_t uak[2];
} aci_uak_pair;

typedef struct signal_user_record signal_user_record;
struct signal_user_record
{
    // Byte layout here must match the layout received from EnclaveLoad e164_aci_pni_uak_tuples.
    uint64_t e164;
    uint64_t aci[2];
    uint64_t pni[2];
    uint64_t uak[2];
};

typedef struct
{
    // Byte layout here must match the layout for ClientResponse e164_pni_aci_triples.
    uint64_t e164;
    uint64_t pni[2];
    uint64_t aci[2];
} e164_pni_aci_triple;

typedef struct aci_index aci_index;

/**
 * @brief Create a randomized, consistent-time aci-uak lookup index that holds `capacity` pairs,
 * takes time `capacity*ceil(logs(capacity))` to load and takes
 * `ceil(log2(capacity))` to query. It is randomized time: the difference in time to load
 * any two set of pairs of size `<= capacity` into this index will be identically distributed
 * to the difference between loading the same data with different keys.
 *
 * @param capacity
 * @param key random key to determine sort order
 * @return aci_index*
 */
aci_index *aci_index_create(size_t capacity, uint8_t key[static 8]);
void aci_index_destroy(aci_index *index);

int aci_index_lookup(const aci_index *index, const uint64_t aci[static 2], uint64_t out_uak[static 2]);

/**
 * @brief Load a set of aci-uak pairs and prepare index in consistent + random time controlled by
 * the `num_pairs_for_timing` variable.
 *
 * @param index
 * @param num_pairs number of aci-uak pairs to insert
 * @param pairs array of aci-uak pairs
 * @return error_t
 */
error_t load_aci_uak_pairs(fixedset_t *index, size_t num_pairs, aci_uak_pair pairs[]);

/**
 * @brief Process user records retrieved from database along with the aci-uak pairs provided by
 * the client to compute the E.164-ACI-PNI triples to return to the client.
 *
 * @param index
 * @param num_e164s Number of E.164s in request
 * @param e164s Array of requested E.164s
 * @param ohtable_response User records for the E.164s that were present in database. Not in order. When
 * an E.164 is not present in the database, a record with all bits set to 1 appears in this array (in particular,
 * the `record.e164 == UINT64_MAX`).
 * @param out_triples The E.164-ACI-PNI triples to return to the client
 * @return error_t
 */
error_t create_e164_pni_aci_triples(
    fixedset_t* index,
    size_t num_e164s,
    uint64_t e164s[num_e164s],
    signal_user_record ohtable_response[num_e164s],
    e164_pni_aci_triple out_triples[num_e164s]);

#endif // CDSI_ENC_ACI_LOOKUP_H
