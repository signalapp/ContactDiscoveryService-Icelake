// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "sharded_ohtable/queue.h"
#include "util/util.h"
#include "util/tests.h"

#define NUM_TEST_INSERTS 1000
#define SMALL_INSERT_BATCH_SIZE 11
#define LARGE_INSERT_BATCH_SIZE 50
#define WRONG_ITEM_RETRIEVED 1

int test_data[NUM_TEST_INSERTS];

void init_test_data()
{
    for (int i = 0; i < NUM_TEST_INSERTS; ++i)
    {
        test_data[i] = i;
    }
}

int test_single_thread_rw()
{
    queue *q = queue_create();

    int data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    for (int i = 0; i < 10; ++i)
    {
        TEST_ERR(queue_add_item(q, data + i));
    }

    for (int i = 0; i < 10; ++i)
    {
        int *it = (int *)queue_next_item(q, true);
        TEST_ASSERT(*it == data[i]);
    }

    queue_destroy(q);
    return err_SUCCESS;
}

void *producer_one_at_a_time(void *in)
{
    queue *q = in;
    for (size_t i = 0; i < NUM_TEST_INSERTS; ++i)
    {
        TEST_ERR(queue_add_item(q, test_data + i));
    }
    return 0;
}


void *consumer(void *in)
{
    queue *q = in;
    int *retval = calloc(1, sizeof(*retval));
    *retval = -1;
    for (int i = 0; i < NUM_TEST_INSERTS; ++i)
    {
        int *it = (int *)queue_next_item(q, true);
        if (*it != test_data[i])
        {
            *retval = i;
            break;
        }
    }
    return retval;
}

int test_multi_thread_read_then_write()
{
    queue *q = queue_create();

    pthread_t prod_tid;
    pthread_t cons_tid;
    pthread_create(&cons_tid, NULL, consumer, q);
    // sleep a little to make sure it is trying to read before we start producing
    sleep(1);
    pthread_create(&prod_tid, NULL, producer_one_at_a_time, q);

    void *prod_retval;
    void *cons_retval;
    pthread_join(prod_tid, &prod_retval);
    pthread_join(cons_tid, &cons_retval);

    TEST_ASSERT(*(int *)cons_retval == -1);

    free(prod_retval);
    free(cons_retval);
    queue_destroy(q);
    return err_SUCCESS;
}

int test_nonblocking_read()
{
    queue *q = queue_create();
    void* a = (void*)1;
    TEST_ERR(queue_add_item(q, a));
    TEST_ASSERT(1 == (long) queue_next_item(q, false));
    TEST_ASSERT(NULL == queue_next_item(q, false));
    queue_destroy(q);
    return 0;
}

int test_closed_write()
{
    queue *q = queue_create();
    void* a = (void*)1;
    TEST_ERR(queue_add_item(q, a));
    queue_close(q);
    TEST_ASSERT(err_QUEUE__CLOSED == queue_add_item(q, a));
    queue_destroy(q);
    return 0;
}

int main()
{
    init_test_data();
    RUN_TEST(test_single_thread_rw());
    RUN_TEST(test_multi_thread_read_then_write());
    RUN_TEST(test_nonblocking_read());
    RUN_TEST(test_closed_write());
    return 0;
}
