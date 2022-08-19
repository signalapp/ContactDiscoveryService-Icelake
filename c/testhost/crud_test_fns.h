// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_TESTHOST_CRUD_TEST_FNS_H
#define CDS_TESTHOST_CRUD_TEST_FNS_H 1

#include <openenclave/host.h>
#include "client_state.h"
#include "aci_pni_uak_helpers/aci_pni_uak_helpers.h"

typedef struct
{
    size_t num_e164s;
    uint64_t *e164s;
    size_t num_pairs;
    aci_uak_pair *aci_uak_pairs;
} client_request;

int test_data_load(oe_enclave_t *enclave, size_t num_records, size_t num_sample_batches, size_t batch_size, signal_user_record sample_record_batches[]);
int run_query_batch(oe_enclave_t *enclave, size_t batch_size, u64 e164s[], uint64_t *elapsed_micros, bool simulate);

int call_ratelimit(enclave_client_state *ecs, client_request req);
int run(enclave_client_state *ecs, size_t num_triples, e164_pni_aci_triple *triples);

error_t set_ratelimit_secret(oe_enclave_t *enclave, size_t size, uint8_t *secret);

#endif // CDS_TESTHOST_CRUD_TEST_FNS_H
