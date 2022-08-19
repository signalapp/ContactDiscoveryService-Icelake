// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include "sharded_ohtable/shard.h"
#include "util/util.h"
#include "util/tests.h"

#define RECORDS_TO_INSERT 1000
#define RECORD_SIZE_QWORDS 7

static u64 mt_insert_records[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];
static u64 mt_queries[RECORDS_TO_INSERT];
static sharded_ohtable_request* mt_requests[RECORDS_TO_INSERT];
static u64 mt_responses[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];

void *worker(void *input)
{
    shard *shard = input;
    shard_run(shard);
    return 0;
}

int test_shard_request_response()
{
    shard *shard = shard_create(0, UINT64_MAX, RECORD_SIZE_QWORDS, 1000000, TEST_STASH_SIZE, getentropy);
    // run the worker
    pthread_t tid;
    pthread_create(&tid, 0, worker, (void *)shard);

    u64 *record = calloc(RECORD_SIZE_QWORDS, sizeof(*record));
    u64 *query_data = calloc(1, sizeof(*query_data));
    *query_data = 1234;
    memset(record, 0, RECORD_SIZE_QWORDS * sizeof(*record));
    record[0] = *query_data;

    u64 output[RECORD_SIZE_QWORDS];

    sharded_ohtable_request* req = shard_query(shard, query_data, output, 1);
    shard_wait(shard);

    TEST_ASSERT(shard_request_response(req)[0] == UINT64_MAX);
    shard_request_destroy(req);

    req = shard_insert(shard, record, 1);
    shard_wait(shard);
    TEST_ASSERT(shard_request_response(req) == 0);
    shard_request_destroy(req);

    req = shard_query(shard, query_data, output, 1);
    shard_wait(shard);

    TEST_ASSERT(shard_request_response(req)[0] == *query_data);
    shard_request_destroy(req);

    // clear the shard
    req = shard_clear(shard);
    shard_wait(shard);
    shard_request_destroy(req);

    // check that the record no longer there
    req = shard_query(shard, query_data, output, 1);
    shard_wait(shard);

    TEST_ASSERT(shard_request_response(req)[0] == UINT64_MAX);
    shard_request_destroy(req);

    shard_stop(shard);

    void *worker_retval;
    pthread_join(tid, &worker_retval);
    free(worker_retval);
    free(query_data);
    free(record);
    shard_destroy(shard);
    return err_SUCCESS;
}

int test_shard_load()
{
    shard *shard = shard_create(0, UINT64_MAX, RECORD_SIZE_QWORDS, 1000000, TEST_STASH_SIZE, getentropy);
    // run the worker
    pthread_t tid;
    pthread_create(&tid, 0, worker, (void *)shard);

    memset(mt_insert_records, 0, sizeof(mt_insert_records));
    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        mt_insert_records[RECORD_SIZE_QWORDS * i] = i;
    }
    for (size_t i = 0; i < RECORDS_TO_INSERT; i++) {
        mt_requests[i] = shard_insert(
            shard, mt_insert_records + RECORD_SIZE_QWORDS * i, 1);
    }
    shard_wait(shard);
    for (size_t i = 0; i < RECORDS_TO_INSERT; i++) {
        TEST_ASSERT(shard_request_response(mt_requests[i]) == 0);
        shard_request_destroy(mt_requests[i]);
    }
    memset(mt_queries, 0, sizeof(mt_queries));
    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        mt_queries[i] = i;
    }

    for (size_t i = 0; i < RECORDS_TO_INSERT; i++) {
        mt_requests[i] = shard_query(
            shard, mt_queries + i, mt_responses + i*RECORD_SIZE_QWORDS, 1);
    }
    shard_wait(shard);
    for (size_t i = 0; i < RECORDS_TO_INSERT; i++) {
        TEST_ASSERT(shard_request_response(mt_requests[i]) != 0);
        TEST_ASSERT(shard_request_response(mt_requests[i])[0] == i);
        shard_request_destroy(mt_requests[i]);
    }

    shard_stop(shard);

    void *worker_retval;
    pthread_join(tid, &worker_retval);
    free(worker_retval);
    shard_destroy(shard);
    return err_SUCCESS;
}

int main()
{
    run_shard_tests();
    RUN_TEST(test_shard_request_response());
    RUN_TEST(test_shard_load());
    return 0;
}
