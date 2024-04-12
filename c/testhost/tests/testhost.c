
// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include <openenclave/host.h>

#include <noise/protocol/handshakestate.h>
#include <noise/protocol/cipherstate.h>
#include <noise/protocol.h>

#include "untrust/cds_u.h"
#include "proto/cdsi.h"
#include "aci_pni_uak_helpers/aci_pni_uak_helpers.h"
#include "testhost/crud_test_fns.h"
#include "testhost/client_state.h"
#include "testhost/checks.h"
#include "util/util.h"
#include "util/statistics.h"
#include "util/tests.h"

#include "testhost/ohtable_stats.h"

#define NUM_SHARDS 3
#define AVAILABLE_BYTES (30 << 20)

struct attest_thread_fn_args {
    oe_enclave_t* enclave;
    size_t num_attestations;
    oe_result_t oe_result;
    int attest_result;
};

void* attest_thread_fn(void* input) {
    struct attest_thread_fn_args* args = (struct attest_thread_fn_args*)input;
    args->oe_result = OE_OK;
    args->attest_result = err_SUCCESS;
    for(size_t i = 0; i < args->num_attestations; ++i) {
        args->oe_result = enclave_attest(args->enclave, &args->attest_result);
        if(args->oe_result != OE_OK) {
            TEST_LOG("OE call error for enclave_attest: %d (%s)", args->oe_result, oe_result_str(args->oe_result));
            break;
        }
        if(args->attest_result != err_SUCCESS) {
            TEST_LOG("enclave_attest failed with code: %d", args->attest_result);
            break;
        }
        sleep(0);
    }
    return NULL;
}

struct handshake_thread_fn_args {
    oe_enclave_t* enclave;
    size_t num_handshakes;
    oe_result_t oe_result;
    int new_client_result;
    int handshake_result;
    int close_result;
};

void* handshake_thread_fn(void* input) {
    struct handshake_thread_fn_args* args = (struct handshake_thread_fn_args*)input;
    args->oe_result = OE_OK;
    args->new_client_result = err_SUCCESS;
    args->handshake_result = err_SUCCESS;
    args->close_result = err_SUCCESS;
    for(size_t i = 0; i < args->num_handshakes; ++i) {

        uint64_t client_id;
        uint8_t ereport[1024];
        size_t ereport_size = 0;
        args->oe_result =  enclave_new_client(args->enclave, &args->new_client_result, &client_id, sizeof(ereport), ereport, &ereport_size);
        if(args->oe_result != OE_OK) {
            TEST_LOG("OE call error for enclave_new_client: %d (%s)", args->oe_result, oe_result_str(args->oe_result));
            break;
        }
        if(args->new_client_result != err_SUCCESS) {
            TEST_LOG("enclave_new_client failed with code: %d", args->new_client_result);
            break;
        }

        uint8_t pubkey[NOISE_KEY_SIZE];
        ENCLAVE_TEST_ERR(extract_public_key(ereport_size, ereport, pubkey));
        enclave_client_state ecs = {.client_id = client_id, .enclave = args->enclave};
        args->handshake_result =  perform_handshake(&ecs, true, pubkey);
        if(args->handshake_result != err_SUCCESS) {
            TEST_LOG("perform_handshake failed with code: %d", args->handshake_result);
            break;
        }


        args->oe_result =  enclave_close_client(args->enclave, &args->close_result, client_id);
        if(args->oe_result != OE_OK) {
            TEST_LOG("OE call error for enclave_close_client: %d (%s)", args->oe_result, oe_result_str(args->oe_result));
            break;
        }
        if(args->close_result != err_SUCCESS) {
            TEST_LOG("enclave_close_client failed with code: %d", args->close_result);
            break;
        }
        sleep(0);
    }

    return NULL;
}

struct thread_fn_args
{
    oe_enclave_t *enclave;
    size_t shard_id;
};

void *shard_thread_fn(void *input)
{
    int retval = 0;
    oe_enclave_t *enclave = ((struct thread_fn_args *)input)->enclave;
    size_t shard_id = ((struct thread_fn_args *)input)->shard_id;
    fprintf(stderr, "shard_thread_fn(%p, %lu) before call enclave\n", enclave, shard_id);
    enclave_run_shard(enclave, &retval, shard_id);
    return NULL;
}

pthread_t run_shard(oe_enclave_t *enclave, size_t shard_id)
{
    pthread_t tid;
    struct thread_fn_args args = {.enclave = enclave, .shard_id = shard_id};
    struct thread_fn_args *thread_input;
    CHECK(thread_input = calloc(1, sizeof args));
    *thread_input = args;

    /* create the threads; may be any number, in general */
    if (pthread_create(&tid, NULL, shard_thread_fn, thread_input) != 0)
    {
        fprintf(stderr, "Unable to create shard %lu thread\n", shard_id);
        exit(1);
    }
    return tid;
}

int load_simple_data(oe_enclave_t *enclave)
{
    int retval = err_SUCCESS;
    uint8_t *workspace = 0;
    uint8_t *encoded = 0;
    signal_user_record *data = 0;

    size_t records_per_block = 5000;
    CHECK(data = calloc(records_per_block, sizeof *data));

    size_t workspace_size = 2 * records_per_block * 64;
    workspace = malloc(workspace_size);
    size_t encoded_size = records_per_block * 56 + 32;
    encoded = malloc(encoded_size);
    struct org_signal_cdsi_enclave_load_t *load_req = org_signal_cdsi_enclave_load_new(workspace, workspace_size);
    if (load_req == 0)
    {
        retval = err_HOST__LOADPB__REQUEST_PB;
        goto exit;
    }

    for (size_t i = 0; i < records_per_block; ++i)
    {
        data[i].e164 = i + 1;
        data[i].aci[0] = 1;
        data[i].aci[1] = i + 1;
        data[i].pni[0] = 2;
        data[i].pni[1] = i + 1;
        data[i].uak[0] = 3;
        data[i].uak[1] = i + 1;
    }

    load_req->clear_all = false;
    load_req->e164_aci_pni_uak_tuples.size = records_per_block * sizeof(*data);
    load_req->e164_aci_pni_uak_tuples.buf_p = (uint8_t *)data;
    load_req->shared_token_secret.size = 0;
    load_req->shared_token_secret.buf_p = 0;

    int len = org_signal_cdsi_enclave_load_encode(load_req, encoded, encoded_size);
    if (len < 0)
    {
        retval = err_HOST__LOADPB__REQUEST_PB;
        goto exit;
    }
    // printf("encoded request. encoded size: %lu array size: %lu\n", len, load_req->e164_aci_pni_uak_tuples.size);
    enclave_load_pb(enclave, &retval, len, encoded);
    if (retval != 0)
    {
        printf("load call failed with code %d\n", retval);
        goto exit;
    }

exit:
    free(data);
    free(workspace);
    free(encoded);
    return retval;
}

int teardown_enclave(oe_enclave_t *enclave, size_t num_shards, pthread_t tids[num_shards])
{
    int retval = err_SUCCESS;
    if (enclave)
    {
        TEST_LOG("Tearing down");
        OPEN_ENCLAVE_CALL_TEST_ERR(enclave_stop_shards(enclave, &retval));
        ENCLAVE_TEST_ERR(retval);
        for (size_t i = 0; i < num_shards; ++i)
        {
            pthread_join(tids[i], 0);
        }
        fprintf(stderr, "shard threads joined\n");
        oe_terminate_enclave(enclave);
    }
    return retval;
}

int setup_enclave(const char *enclave_filename, oe_enclave_t **enclave, size_t available_bytes, size_t num_shards, pthread_t tids[num_shards])
{
    int retval = err_SUCCESS;
    uint32_t flags = OE_ENCLAVE_FLAG_SIMULATE|OE_ENCLAVE_FLAG_DEBUG_AUTO;
    size_t stash_overflow_size = TEST_STASH_SIZE;

    // Create the enclave
    fprintf(stderr, "creating enclave\n");
    OPEN_ENCLAVE_CALL_TEST_ERR(oe_create_cds_enclave(enclave_filename, OE_ENCLAVE_TYPE_AUTO, flags, NULL, 0, enclave));

    // Call into enclave to initialize
    fprintf(stderr, "initializing enclave\n");
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_init(*enclave, &retval, available_bytes, 1.6, num_shards, stash_overflow_size));
    ENCLAVE_TEST_ERR(retval);
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_attest(*enclave, &retval));
    ENCLAVE_TEST_ERR(retval);

    for (size_t i = 0; i < num_shards; ++i)
    {
        tids[i] = run_shard(*enclave, i);
        sleep(0);
    }
    sleep(0);

    if (retval != err_SUCCESS)
    {
        teardown_enclave(*enclave, num_shards, tids);
        *enclave = NULL;
    }
    fprintf(stderr, "enclave initialized\n");
    return retval;
}

int test_first_request(oe_enclave_t *enclave)
{
    int retval = err_SUCCESS;

    TEST_LOG("setting secret");
    uint8_t secret[] = {1, 2, 3};
    ENCLAVE_TEST_ERR(set_ratelimit_secret(enclave, sizeof(secret), secret));

    TEST_LOG("loading data");
    ENCLAVE_TEST_ERR(load_simple_data(enclave));

    TEST_LOG("new client");
    uint64_t client_id;
    uint8_t ereport[1024];
    size_t ereport_size = 0;
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_new_client(enclave, &retval, &client_id, sizeof(ereport), ereport, &ereport_size));
    ENCLAVE_TEST_ERR(retval);

    TEST_LOG("public key");
    uint8_t pubkey[NOISE_KEY_SIZE];
    ENCLAVE_TEST_ERR(extract_public_key(ereport_size, ereport, pubkey));
    TEST_LOG("handshaking");
    enclave_client_state ecs = {.client_id = client_id, .enclave = enclave};
    ENCLAVE_TEST_ERR(perform_handshake(&ecs, true, pubkey));

    client_request req = {.num_e164s = 10, .num_pairs = 3};
    uint64_t e164s[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    aci_uak_pair pairs[3];
    for (size_t i = 0; i < 3; ++i)
    {
        pairs[i].aci[0] = 1;
        pairs[i].aci[1] = i + 1;
        pairs[i].uak[0] = 3;
        pairs[i].uak[1] = i + 1;
    }
    req.aci_uak_pairs = pairs;
    req.e164s = e164s;

    TEST_LOG("rate limit call");
    ENCLAVE_TEST_ERR(call_ratelimit(&ecs, req));

    TEST_LOG("run call");
    e164_pni_aci_triple *triples;
    CHECK(triples = calloc(req.num_e164s, sizeof(*triples)));
    ENCLAVE_TEST_ERR(run(&ecs, req.num_e164s, triples));

    for (size_t i = 0; i < req.num_e164s; ++i)
    {
        ENCLAVE_TEST_ASSERT(triples[i].pni[0] == 2 && triples[i].pni[1] == triples[i].e164);
        ENCLAVE_TEST_ASSERT(triples[i].e164 <= 3
                             ? (triples[i].aci[0] == 1 && triples[i].aci[1] == triples[i].e164)
                             : (triples[i].aci[0] == 0 && triples[i].aci[1] == 0));
    }

    TEST_LOG("closing");
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_close_client(enclave, &retval, client_id));
    ENCLAVE_TEST_ERR(retval);

    // check health statistics for anomalies
    ohtable_statistics stats[NUM_SHARDS];
    uint8_t stats_pb[NUM_SHARDS*14*50 + 128];
    size_t actual_out;
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_table_statistics(enclave, &retval, sizeof(stats_pb), stats_pb, &actual_out));
    ENCLAVE_TEST_ERR(retval);

    ENCLAVE_TEST_ERR(decode_statistics(actual_out, stats_pb, stats, NUM_SHARDS));

    size_t total_records = 0;
    for(size_t i = 0; i < NUM_SHARDS; ++i) {
        TEST_LOG("num_items: %zu max_overflow: %zu max_trace: %zu mean_overflow: %lf",
            stats[i].num_items, stats[i].max_stash_overflow_count, stats[i].max_trace_length, ((double)stats[i].sum_stash_overflow_count)/stats[i].oram_access_count);
        total_records += stats[i].num_items;
    }

    // We inserted 5000 records, and each shard has a default "0" record inserted at creation
    TEST_ASSERT(total_records == 5000 + NUM_SHARDS);
    return retval;
}

error_t test_mutithread_attest_and_handshake(oe_enclave_t *enclave) {
    error_t result = err_SUCCESS;
    size_t num_attests = 100;
    size_t num_handshakes = 100;

    struct handshake_thread_fn_args hs_args[] = {{.enclave = enclave, .num_handshakes = num_handshakes},{.enclave = enclave, .num_handshakes = num_handshakes}};
    struct attest_thread_fn_args att_args = {.enclave = enclave, .num_attestations = num_attests};

    pthread_t tids[3];
    if(pthread_create(tids + 0, NULL, handshake_thread_fn, &hs_args[0]) != 0) {
        TEST_LOG("Failed to create first handshake thread");

    }
    if(pthread_create(tids + 1, NULL, attest_thread_fn, &att_args) != 0) {
        TEST_LOG("Failed to create attester thread");

    }
    if(pthread_create(tids + 2, NULL, handshake_thread_fn, &hs_args[1]) != 0) {
        TEST_LOG("Failed to create second handshake thread");

    }

    for (size_t i = 0; i < 3; ++i)
    {
        pthread_join(tids[i], 0);
    }

    if(hs_args[0].oe_result != OE_OK || hs_args[1].oe_result != OE_OK || att_args.oe_result != OE_OK) {
        return err_HOST__GENERAL_OE_ERROR;
    }
    for(size_t i = 0; i < 2; ++i) {
        if(hs_args[i].new_client_result != err_SUCCESS) return hs_args[i].new_client_result;
        if(hs_args[i].handshake_result != err_SUCCESS) return hs_args[i].handshake_result;
        if(hs_args[i].close_result != err_SUCCESS) return hs_args[i].close_result;
    }
    if (att_args.attest_result != err_SUCCESS) {
        return att_args.attest_result;
    }
    return result;
}

int test_empty_request(oe_enclave_t *enclave)
{
    int retval = err_SUCCESS;

    // set shared secret
    uint8_t secret[] = {1, 2, 3};
    ENCLAVE_TEST_ERR(set_ratelimit_secret(enclave, sizeof(secret), secret));

    // load data
    ENCLAVE_TEST_ERR(load_simple_data(enclave));

    uint64_t client_id;
    uint8_t ereport[1024];
    size_t ereport_size = 0;
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_new_client(enclave, &retval, &client_id, sizeof(ereport), ereport, &ereport_size));
    ENCLAVE_TEST_ERR(retval);

    uint8_t pubkey[NOISE_KEY_SIZE];
    ENCLAVE_TEST_ERR(extract_public_key(ereport_size, ereport, pubkey));
    enclave_client_state ecs = {.client_id = client_id, .enclave = enclave};
    ENCLAVE_TEST_ERR(perform_handshake(&ecs, true, pubkey));

    client_request req = {.num_e164s = 0, .num_pairs = 3};
    uint64_t e164s[] = {};
    aci_uak_pair pairs[3];
    for (size_t i = 0; i < 3; ++i)
    {
        pairs[i].aci[0] = 1;
        pairs[i].aci[1] = i + 1;
        pairs[i].uak[0] = 3;
        pairs[i].uak[1] = i + 1;
    }
    req.aci_uak_pairs = pairs;
    req.e164s = e164s;

    ENCLAVE_TEST_ASSERT(err_ENCLAVE__RATELIMIT__REQUEST_PB_INVALID == call_ratelimit(&ecs, req));
    return retval;
}

int test_invalid_client_ids(oe_enclave_t* enclave)
{
    uint8_t in_buf[8];
    uint8_t out_buf[8];
    size_t out_buf_size = sizeof(out_buf);
    int retval = err_SUCCESS;

    TEST_LOG("Trying client call with invalid client ID");
    ENCLAVE_TEST_ASSERT(0 == enclave_run(
        enclave, &retval, 123, 0, sizeof(in_buf), in_buf, out_buf_size, out_buf, &out_buf_size));
    ENCLAVE_TEST_ASSERT(err_ENCLAVE__GENERAL__CLIENT_GET_FAILED == retval);

    TEST_LOG("Trying client close with invalid client ID");
    ENCLAVE_TEST_ASSERT(0 == enclave_close_client(enclave, &retval, 123));
    ENCLAVE_TEST_ASSERT(err_ENCLAVE__GENERAL__CLIENT_REMOVE_FAILED == retval);

    TEST_LOG("Creating and closing valid client ID");
    uint64_t valid = 0;
    uint8_t valid_out[4096];
    size_t valid_out_size = sizeof(valid_out);
    ENCLAVE_TEST_ASSERT(0 == enclave_new_client(
        enclave, &retval, &valid, valid_out_size, valid_out, &valid_out_size));
    ENCLAVE_TEST_ASSERT(err_SUCCESS == retval);
    ENCLAVE_TEST_ASSERT(0 != valid);

    ENCLAVE_TEST_ASSERT(0 == enclave_close_client(enclave, &retval, valid));
    ENCLAVE_TEST_ASSERT(err_SUCCESS == retval);

    TEST_LOG("Attempting to re-close valid client ID (double-free)");
    ENCLAVE_TEST_ASSERT(0 == enclave_close_client(enclave, &retval, valid));
    ENCLAVE_TEST_ASSERT(err_ENCLAVE__GENERAL__CLIENT_REMOVE_FAILED == retval);

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(
            stderr, "Usage: %s enclave_image_path\n", argv[0]);
        return 1;
    }
    const char *enclave_filename = argv[1];
    oe_enclave_t *enclave = NULL;
    pthread_t tids[NUM_SHARDS];
    RUN_TEST(setup_enclave(enclave_filename, &enclave, AVAILABLE_BYTES, NUM_SHARDS, tids));
    RUN_TEST(test_first_request(enclave));
    RUN_TEST(test_mutithread_attest_and_handshake(enclave));
    RUN_TEST(test_empty_request(enclave));
    RUN_TEST(test_invalid_client_ids(enclave));
    RUN_TEST(teardown_enclave(enclave, NUM_SHARDS, tids));
    return 0;
}
