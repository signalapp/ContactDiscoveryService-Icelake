// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <halfsiphash.h>
#include "util/util.h"
#include "aci_pni_uak_helpers.h"
#include "fixedset/fixedset.h"

error_t load_aci_uak_pairs(fixedset_t *index, size_t num_pairs, aci_uak_pair pairs[])
{
    for (size_t i = 0; i < num_pairs; i++)
    {
      // We use this as a set, with no value and key (aci,uak).
      RETURN_IF_ERROR(fixedset_upsert(index, &pairs[i], NULL));
    }
    return err_SUCCESS;
}

#define ZERO_OR_U64MAX(u64v) ((u64v) == 0 ? 0 : UINT64_MAX)

error_t create_e164_pni_aci_triples(
    fixedset_t* index,
    size_t num_e164s,
    uint64_t e164s[num_e164s],
    signal_user_record ohtable_response[num_e164s],
    e164_pni_aci_triple out_triples[num_e164s])
{
    memset(out_triples, 0, num_e164s * sizeof(*out_triples));
    for (uint64_t i = 0; i < (uint64_t) num_e164s; ++i)
    {
        // copy locally
        signal_user_record r = ohtable_response[i];
        // We consider the record empty if EITHER:  e164 == UINT64_MAX  OR  r.aci is all zeros
        // We do this because we don't currently have a "remove from ohtable" function, so we
        // instead do a "remove" by overwriting the record for the e164 to all zeros.
        // The following line should be constant time, since it's bitwise.
        uint64_t got_and = ZERO_OR_U64MAX(~r.e164) & (ZERO_OR_U64MAX(r.aci[0]) | ZERO_OR_U64MAX(r.aci[1]));
        // munge uak[0]=i in case where e164 not found, so hash lookups are all different
        r.uak[0] = (r.uak[0] & got_and) | (i & ~got_and);

        // We want to search for an ACI/UAK pair, but they're not contiguously laid
        // out in the signal_user_record, so we need to make our own pair.
        aci_uak_pair pair = { .aci = { r.aci[0], r.aci[1] }, .uak = { r.uak[0], r.uak[1] } };
        // We store the UAK as all zeros in the OHTable if there isn't an associated UAK,
        // in which case we never want to return the ACI.  We do the lookup into the
        // map regardless, but then check to make sure the UAK is nonzero as well.
        bool add_aci = fixedset_get(index, &pair) & ((r.uak[0] | r.uak[1]) != 0);
        uint64_t aci_and = add_aci ? UINT64_MAX : 0;
        // This should give us:
        //   - Everything all zeros, if e164 was not found
        //   - ACI all zeros, if ACI/UAK pair was not found
        out_triples[i].e164 =   got_and &           r.e164;
        out_triples[i].pni[0] = got_and &           r.pni[0];
        out_triples[i].pni[1] = got_and &           r.pni[1];
        out_triples[i].aci[0] = got_and & aci_and & r.aci[0];
        out_triples[i].aci[1] = got_and & aci_and & r.aci[1];
    }

    return err_SUCCESS;
}
