// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "util/util.h"
#include "queue.h"

#define QUEUE_SIZE 20
struct queue
{
    queue_item items[QUEUE_SIZE];

    size_t num_items;
    size_t add;
    size_t rem;
    bool closed;  // when closed, don't accept any more writes

    pthread_mutex_t mutex;
    pthread_cond_t consumer_con;
    pthread_cond_t producer_con;
};

size_t queue_size_bytes() {
    return sizeof(queue);
}

queue *queue_create()
{
    queue *q;
    CHECK(q = calloc(1, sizeof(*q)));
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->consumer_con, NULL);
    pthread_cond_init(&q->producer_con, NULL);
    return q;
}
void queue_close(queue* q)
{
  pthread_mutex_lock(&q->mutex);
  q->closed = true;
  pthread_cond_broadcast(&q->producer_con);
  pthread_mutex_unlock(&q->mutex);
}

void queue_destroy(queue *q)
{
    if (q)
    {
        pthread_mutex_destroy(&q->mutex);
        pthread_cond_destroy(&q->consumer_con);
        pthread_cond_destroy(&q->producer_con);
        free(q);
    }
}

error_t queue_add_item(queue *q, queue_item item)
{
    pthread_mutex_lock(&q->mutex);
    CHECK(q->num_items <= QUEUE_SIZE);
    while (q->num_items == QUEUE_SIZE && !q->closed)
    { /* block if buffer is full */
        pthread_cond_wait(&q->producer_con, &q->mutex);
    }
    error_t err = err_SUCCESS;
    if (q->closed) {
      err = err_QUEUE__CLOSED;
    } else {
      q->items[q->add] = item;
      q->add = (q->add + 1) % QUEUE_SIZE;
      q->num_items++;
      pthread_cond_signal(&q->consumer_con);
    }
    if (q->num_items < QUEUE_SIZE)
    {
        pthread_cond_signal(&q->producer_con);
    }
    pthread_mutex_unlock(&q->mutex);
    return err;
}

queue_item queue_next_item(queue *q, bool block)
{
    queue_item result = NULL;
    pthread_mutex_lock(&q->mutex); /* overflow */
    if (block || q->num_items > 0) {
      while (q->num_items == 0)
      { /* block if buffer is empty */
          pthread_cond_wait(&q->consumer_con, &q->mutex);
      }
      /* if executing here, buffer not empty so remove element */
      result = q->items[q->rem];
      q->rem = (q->rem + 1) % QUEUE_SIZE;
      q->num_items--;
      pthread_cond_signal(&q->producer_con);
      if (q->num_items > 0)
      {
          pthread_cond_signal(&q->consumer_con);
      }
    }
    pthread_mutex_unlock(&q->mutex);
    return result;
}
