// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdlib.h>
#include <pthread.h>
#include "ohtable/ohtable.h"
#include "shard.h"
#include "util/util.h"

struct shard
{
    u64 lb;
    u64 ub;
    size_t record_size_qwords;
    int keep_alive;
    ohtable *table;
    queue *requests;
};

typedef struct {
    // Thread-safe waiting for this request to complete.
    // On completion (shard_waiter_finish), `done` is set to true.
    // Waiting (shard_request_wait) waits for this to be the case.
    pthread_mutex_t done_lock;
    bool done;
    pthread_cond_t done_cond;
} shard_request_waiter;

struct sharded_ohtable_request
{
    // Request type
    sharded_ohtable_request_type type;
    // Pointer to (not-owned) memory containing the request
    const u64 *request;
    // Pointer to (not-owned) memory containing the response
    u64* response;
    // Any error that occurred during the execution of this request
    error_t err;
    // If non-NULL, complete this waiter.
    shard_request_waiter* wait;
    // Size of the request in "request size" increments
    size_t batch_size;
};

error_t shard_request_error(sharded_ohtable_request* r) { return r->err; }
u64* shard_request_response(sharded_ohtable_request* r) { return r->response; }

void shard_add_zero_record(shard *shard)
{

    u64 *zero_record;
    CHECK(zero_record = calloc(shard->record_size_qwords, sizeof(*zero_record)));
    ohtable_put(shard->table, zero_record);
    free(zero_record);
}

static shard* _create(ohtable* table, u64 lb, u64 ub, size_t record_size_qwords) {

    shard *shard;
    CHECK(shard = calloc(1, sizeof(*shard)));
    shard->lb = lb;
    shard->ub = ub;
    shard->record_size_qwords = record_size_qwords;
    shard->keep_alive = 0;
    shard->table = table;
    shard->requests = queue_create();
    // every shard has a "zero record" inserted - this is the result for a dummy request
    // meant to balance memory accesses between shards
    shard_add_zero_record(shard);
    return shard;
}

shard* shard_create_for_available_mem(u64 lb, u64 ub, size_t record_size_qwords, size_t available_bytes, double load_factor, size_t stash_overflow_size, entropy_func getentropy) {
    available_bytes -= (sizeof(shard) + queue_size_bytes());
    ohtable* table = ohtable_create_for_available_mem(record_size_qwords, available_bytes, load_factor, stash_overflow_size, getentropy);
    shard* shard = _create(table, lb, ub, record_size_qwords);
    shard_add_zero_record(shard);
    return shard;
}

shard *shard_create(u64 lb, u64 ub, size_t record_size_qwords, size_t record_capacity, size_t stash_overflow_size, entropy_func getentropy)
{
    ohtable* table = ohtable_create(record_capacity, record_size_qwords, stash_overflow_size, getentropy);
    shard* shard = _create(table, lb, ub, record_size_qwords);
    shard_add_zero_record(shard);
    return shard;
}

static shard_request_waiter* shard_waiter_create() {
    shard_request_waiter* w;
    CHECK(w = calloc(1, sizeof(*w)));
    w->done = false;
    CHECK(0 == pthread_mutex_init(&w->done_lock, NULL));
    CHECK(0 == pthread_cond_init(&w->done_cond, NULL));
    return w;
}

static sharded_ohtable_request *shard_request_create(sharded_ohtable_request_type type, const u64 *data, u64* response, shard_request_waiter* w, size_t batch_size)
{
    sharded_ohtable_request *r;
    CHECK(r = calloc(1, sizeof(*r)));
    r->type = type;
    r->request = data;
    r->response = response;
    r->wait = w;
    r->batch_size = batch_size;
    return r;
}

void shard_request_destroy(sharded_ohtable_request *req)
{
    if (!req) return;
    free(req);
}

static void shard_waiter_destroy(shard_request_waiter* w) {
    if (!w) return;
    CHECK(0 == pthread_cond_destroy(&w->done_cond));
    CHECK(0 == pthread_mutex_destroy(&w->done_lock));
    free(w);
}

static void shard_request_waiter_wait(shard_request_waiter* w) {
    CHECK(0 == pthread_mutex_lock(&w->done_lock));
    while (!w->done) {
      CHECK(0 == pthread_cond_wait(&w->done_cond, &w->done_lock));
    }
    CHECK(0 == pthread_mutex_unlock(&w->done_lock));
}

void shard_wait(shard* shard)
{
    shard_request_waiter* w = shard_waiter_create();
    sharded_ohtable_request *r = shard_request_create(shard_request_wait, NULL, NULL, w, 0);
    CHECK(err_SUCCESS == queue_add_item(shard->requests, r));
    shard_request_waiter_wait(w);
    shard_waiter_destroy(w);
    shard_request_destroy(r);
}

static void shard_waiter_finish(shard_request_waiter* w)
{
    // Mark this request complete by unlocking the `done` mutex to allow
    // shard_request_wait to complete.
    CHECK(0 == pthread_mutex_lock(&w->done_lock));
    w->done = true;
    // We could probably _signal here, but this future-proofs us in case
    // there are potentially more waiters.
    CHECK(0 == pthread_cond_broadcast(&w->done_cond));
    CHECK(0 == pthread_mutex_unlock(&w->done_lock));
}

void shard_destroy(shard *shard)
{
    if (shard)
    {
        queue_close(shard->requests);
        sharded_ohtable_request* req;
        while (NULL != (req = queue_next_item(shard->requests, false))) {
          req->err = err_SHARD__DESTROYING;
          if (req->wait) shard_waiter_finish(req->wait);
        }
        ohtable_destroy(shard->table);
        queue_destroy(shard->requests);
        free(shard);
    }
}


ohtable_statistics* shard_report_ohtable_statisitics(shard* shard) {
    return ohtable_statistics_create(shard->table);
}

sharded_ohtable_request* shard_clear(shard *shard)
{
    sharded_ohtable_request *r = shard_request_create(shard_request_clear, NULL, NULL, NULL, 0);
    CHECK(err_SUCCESS == queue_add_item(shard->requests, r));
    return r;
}

static void shard_do_clear(shard *shard)
{
    ohtable_clear(shard->table);
    shard_add_zero_record(shard);
}

u64 shard_lb(const shard *shard)
{
    return shard->lb;
}

u64 shard_ub(const shard *shard)
{
    return shard->ub;
}

bool shard_contains(const shard *shard, u64 key)
{
    // & rather than && for constant-time.
    return (shard->lb <= key) & (key < shard->ub);
}

static sharded_ohtable_request *shard_next_request(shard *shard)
{
    return (sharded_ohtable_request *)queue_next_item(shard->requests, true);
}

sharded_ohtable_request* shard_insert(shard *shard, const u64 *record, size_t num_records)
{
    sharded_ohtable_request *r = shard_request_create(shard_request_insert, record, NULL, NULL, num_records);
    CHECK(err_SUCCESS == queue_add_item(shard->requests, r));
    return r;
}

sharded_ohtable_request* shard_query(shard *shard, const u64 *key, u64* response, size_t num_queries)
{
    sharded_ohtable_request *r = shard_request_create(shard_request_query, key, response, NULL, num_queries);
    CHECK(err_SUCCESS == queue_add_item(shard->requests, r));
    return r;
}

void shard_handle_request(shard *shard, sharded_ohtable_request *req)
{
    CHECK(req);
    switch (req->type)
    {
    case shard_request_insert:
        CHECK(req->request);
        for (size_t i = 0; i < req->batch_size; i++) {
          if (err_SUCCESS != (req->err = ohtable_put(shard->table, req->request + shard->record_size_qwords * i))) break;
        }
        break;
    case shard_request_query:
        CHECK(req->request);
        CHECK(req->response);
        for (size_t i = 0; i < req->batch_size; i++) {
          if (err_SUCCESS != (req->err = ohtable_get(shard->table, req->request[i], req->response + shard->record_size_qwords * i))) break;
        }
        break;
    case shard_request_stop:
        shard->keep_alive = 0;
        break;
    case shard_request_clear:
        shard_do_clear(shard);
        break;
    case shard_request_wait:
        break;
    default:
        TEST_LOG("received unsupported request type %d", req->type);
        CHECK(false);
        break;
    }
    if (req->wait) shard_waiter_finish(req->wait);
}

void shard_run(shard *shard)
{
    shard->keep_alive = 1;
    while (shard->keep_alive)
    {
        sharded_ohtable_request *req = shard_next_request(shard);
        shard_handle_request(shard, req);
    }
}
void shard_stop(shard *shard)
{
    shard->keep_alive = 0;
    shard_request_waiter* w = shard_waiter_create();
    sharded_ohtable_request *req = shard_request_create(shard_request_stop, NULL, NULL, w, 0);
    CHECK(err_SUCCESS == queue_add_item(shard->requests, req));
    shard_request_waiter_wait(w);
    shard_waiter_destroy(w);
    shard_request_destroy(req);
}

#ifdef IS_TEST
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/random.h>
#include "util/util.h"
#include "util/tests.h"

#define RECORDS_TO_INSERT 1000
#define RECORD_SIZE_QWORDS 7

static u64 mt_insert_records[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];
static u64 mt_queries[RECORDS_TO_INSERT];
static u64 mt_responses[RECORD_SIZE_QWORDS * RECORDS_TO_INSERT];
static sharded_ohtable_request* mt_requests[RECORDS_TO_INSERT];

static void* consumer_mt(void* v_shard) {
  shard* shard = v_shard;
  for (int i = 0; i < RECORDS_TO_INSERT + /*number of shard_wait calls*/ 1; i++) {
    shard_handle_request(shard, shard_next_request(shard));
  }
  return NULL;
}

int test_shard_receives_inserts_mt()
{
    shard *shard = shard_create(0, UINT64_MAX, RECORD_SIZE_QWORDS, 1000000, TEST_STASH_SIZE, getentropy);
    pthread_t cons_tid;
    pthread_create(&cons_tid, NULL, consumer_mt, shard);

    memset(mt_insert_records, 0, RECORD_SIZE_QWORDS * RECORDS_TO_INSERT * 8);
    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        mt_insert_records[RECORD_SIZE_QWORDS * i] = i+1;
    }

    for (int i = 0; i < RECORDS_TO_INSERT; i++) {
        mt_requests[i] = shard_insert(shard, mt_insert_records + RECORD_SIZE_QWORDS * i, 1);
    }

    shard_wait(shard);

    for (int i = 0; i < RECORDS_TO_INSERT; i++) {
        TEST_ERR(mt_requests[i]->err);
        shard_request_destroy(mt_requests[i]);
    }

    void *cons_retval;
    pthread_join(cons_tid, &cons_retval);
    shard_destroy(shard);
    return 0;
}

int test_shard_receives_queries_mt()
{
    shard *shard = shard_create(0, UINT64_MAX, RECORD_SIZE_QWORDS, 1000000, TEST_STASH_SIZE, getentropy);
    pthread_t cons_tid;
    pthread_create(&cons_tid, NULL, consumer_mt, shard);

    memset(mt_queries, 0, RECORDS_TO_INSERT * 8);
    for (size_t i = 0; i < RECORDS_TO_INSERT; ++i)
    {
        mt_queries[RECORD_SIZE_QWORDS * i] = i+1;
    }

    for (int i = 0; i < RECORDS_TO_INSERT; i++) {
        mt_requests[i] = shard_query(shard, mt_queries + i, mt_responses + i*RECORD_SIZE_QWORDS, 1);
    }

    shard_wait(shard);

    for (int i = 0; i < RECORDS_TO_INSERT; i++) {
        TEST_ERR(mt_requests[i]->err);
        shard_request_destroy(mt_requests[i]);
    }

    void *cons_retval;
    pthread_join(cons_tid, &cons_retval);
    shard_destroy(shard);
    return 0;
}

int test_shard_handle_request()
{
    shard *shard = shard_create(0, UINT64_MAX, RECORD_SIZE_QWORDS, 1000000, TEST_STASH_SIZE, getentropy);

    // first try the query and see that it isn't there
    {
      u64 query_data[1] = {1234};
      u64 response[RECORD_SIZE_QWORDS];
      sharded_ohtable_request* query_req = shard_request_create(shard_request_query, query_data, response, NULL, 1);
      shard_handle_request(shard, query_req);
      TEST_ERR(query_req->err);
      TEST_ASSERT(response[0] == UINT64_MAX);
      shard_request_destroy(query_req);
    }

    u64 insert_record[RECORD_SIZE_QWORDS] = {1234, 1, 2, 3, 4, 5, 6};
    // then insert and get response
    {
      sharded_ohtable_request* insert_req = shard_request_create(shard_request_insert, insert_record, NULL, NULL, 1);
      shard_handle_request(shard, insert_req);
      TEST_ERR(insert_req->err);
      shard_request_destroy(insert_req);
    }

    // then query again and confirm it is there
    {
      u64 query_data[1] = {1234};
      u64 response[RECORD_SIZE_QWORDS];
      sharded_ohtable_request* query_req = shard_request_create(shard_request_query, query_data, response, NULL, 1);
      shard_handle_request(shard, query_req);
      TEST_ERR(query_req->err);
      TEST_ASSERT(sizeof(insert_record) == sizeof(response));
      TEST_ASSERT(0 == memcmp(response, insert_record, sizeof(insert_record)));
      shard_request_destroy(query_req);
    }

    shard_destroy(shard);

    return err_SUCCESS;
}

void run_shard_tests()
{
    RUN_TEST(test_shard_receives_inserts_mt());
    RUN_TEST(test_shard_receives_queries_mt());
    RUN_TEST(test_shard_handle_request());
}
#endif // IS_TEST
