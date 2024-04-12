// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <openenclave/host.h>
#include <inttypes.h>
#include <stdio.h>

#include <endian.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include <noise/protocol/handshakestate.h>
#include <noise/protocol/cipherstate.h>
#include <noise/protocol.h>

#include "util/statistics.h"

#include "untrust/cds_u.h"
#include "proto/cdsi.h"
#include "aci_pni_uak_helpers/aci_pni_uak_helpers.h"

#include "crud_test_fns.h"
#include "client_state.h"
#include "checks.h"
#include "ohtable_stats.h"


#define QUERIES_PER_BATCH 500
#define AVAILABLE_BYTES (4ul << 30)
#define NUM_SHARDS 10
#define STASH_OVERFLOW_SIZE 60
#define NUM_RECORDS_TO_LOAD 1500000

bool check_simulate_opt(int *argc, const char *argv[])
{
    for (int i = 0; i < *argc; i++)
    {
        if (strcmp(argv[i], "--simulate") == 0)
        {
            fprintf(stdout, "Running in simulation mode\n");
            memmove(&argv[i], &argv[i + 1], (*argc - i) * sizeof(char *));
            (*argc)--;
            return true;
        }
    }
    return false;
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
    printf("shard_thread_fn(%p, %lu) before call enclave\n", enclave, shard_id);
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_run_shard(enclave, &retval, shard_id));
    ENCLAVE_TEST_ASSERT(retval == err_SUCCESS);
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

static void print_table_stats(const ohtable_statistics* stats) {
    fprintf(stderr, "TABLE num_items: %zu capacity: %zu mean_displacement: %lf max_trace: %zu\n",
            stats->num_items, stats->capacity, ((double)stats->total_displacement)/stats->num_items, stats->max_trace_length);
    fprintf(stderr, "ORAM overview\n access_count: %zu  num_levels: %zu\n", stats->oram_access_count, stats->oram_recursion_depth);
    fprintf(stderr, "ORAM health\n stash size (EMA10K): %lf  stash max: %zu stash size: %zu mean stash size: %lf\n",
            stats->stash_overflow_ema10k, stats->max_stash_overflow_count, stats->stash_overflow_count, ((double)stats->sum_stash_overflow_count)/stats->oram_access_count);
    fprintf(stderr, "Position map ORAM health\n stash size (EMA10K): %lf  stash max: %zu stash size: %zu\n",
            stats->posmap_stash_overflow_ema10k, stats->posmap_max_stash_overflow_count, stats->posmap_stash_overflow_count);
}

static int cmpu64(const void* a, const void* b) {
    u64 ua = be64toh(*(u64*)a);
    u64 ub = be64toh(*(u64*)b);
    return (ua > ub) - (ua < ub);
}

int main(int argc, const char *argv[])
{
    int ret = 0;
    oe_enclave_t *enclave = NULL;

    uint32_t flags = 0;
    bool simulate = false;
    if (check_simulate_opt(&argc, argv))
    {
        flags |= OE_ENCLAVE_FLAG_SIMULATE;
        simulate = true;
    }

    if (argc != 2)
    {
        fprintf(
            stderr, "Usage: %s enclave_image_path [ --simulate  ]\n", argv[0]);
        goto exit;
    }

    // Create the enclave
    printf("creating enclave\n");
    OPEN_ENCLAVE_CALL_TEST_ERR(oe_create_cds_enclave(argv[1], OE_ENCLAVE_TYPE_AUTO, flags, NULL, 0, &enclave));

    // Call into enclave to initialize
    int retval = 0;
    pthread_t tids[NUM_SHARDS];
    printf("initializing enclave\n");
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_init(enclave, &retval, AVAILABLE_BYTES, 1.6, NUM_SHARDS, STASH_OVERFLOW_SIZE));
    ENCLAVE_TEST_ERR(retval);

    for (size_t i = 0; i < NUM_SHARDS; ++i)
    {
        tids[i] = run_shard(enclave, i);
        sleep(0);
    }
    fprintf(stderr, "enclave initialized\n");

    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_attest(enclave, &retval));
    ENCLAVE_TEST_ERR(retval);

    // set shared secret
    uint8_t secret[] = {1, 2, 3};
    ENCLAVE_TEST_ERR(set_ratelimit_secret(enclave, sizeof(secret), secret));

    // load data
    size_t num_sample_batches = 100;
    size_t batch_size = 500;
    uint64_t elapsed_micros[100];
    signal_user_record *samples;
    u64* e164s;
    CHECK(samples = calloc(num_sample_batches * batch_size, sizeof(*samples)));
    CHECK(e164s = calloc(num_sample_batches * batch_size, sizeof(*e164s)));

    ENCLAVE_TEST_ERR(test_data_load(enclave, NUM_RECORDS_TO_LOAD, num_sample_batches, batch_size, samples));

    ohtable_statistics stats[NUM_SHARDS];
    uint8_t stats_pb[NUM_SHARDS*14*50 + 128];
    size_t actual_out;
    OPEN_ENCLAVE_CALL_TEST_ERR(enclave_table_statistics(enclave, &retval, sizeof(stats_pb), stats_pb, &actual_out));
    ENCLAVE_TEST_ERR(retval);

    ENCLAVE_TEST_ERR(decode_statistics(actual_out, stats_pb, stats, NUM_SHARDS));

    TEST_LOG("\nTABLE statistics after load");
    for(size_t i = 0; i < NUM_SHARDS; ++i) {
        fprintf(stderr, "\nSHARD %zu STATS\n", i);
        print_table_stats(stats + i);
    }

    // prep e164s
    for(size_t i = 0; i < num_sample_batches * batch_size; ++i) {
        e164s[i] = samples[i].e164;
    }



    // Performance analysis on queries
    for (size_t i = 0; i < num_sample_batches; ++i)
    {
        qsort(e164s + i*batch_size, batch_size, sizeof(*e164s), cmpu64);
        GOTO_IF_ERROR(run_query_batch(enclave, batch_size, e164s + i * batch_size, elapsed_micros + i, simulate), exit);
    }
    for (size_t i = 0; i < num_sample_batches; ++i)
    {
        printf("%" PRIu64 ", ", elapsed_micros[i]);
    }
    printf("\n");

exit:
    // Clean up the enclave if we created one
    if (enclave)
    {

        OPEN_ENCLAVE_CALL_TEST_ERR(enclave_stop_shards(enclave, &retval));
        ENCLAVE_TEST_ERR(retval);

        for (size_t i = 0; i < NUM_SHARDS; ++i)
        {
            pthread_join(tids[i], 0);
        }
        fprintf(stderr, "shard threads joined\n");
        OPEN_ENCLAVE_CALL_TEST_ERR(oe_terminate_enclave(enclave));
    }

    return ret;
}
