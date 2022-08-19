// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef LIBORAM_SHARDED_OHTABLE_QUEUE_H
#define LIBORAM_SHARDED_OHTABLE_QUEUE_H 1

#include <stddef.h>
#include <stdbool.h>
#include "util/util.h"
/**
 * @brief A thread-safe blocking FIFO queue. Inserts will block until
 * there is room. Reads will block until items are available.
 *
 */

typedef struct queue queue;
typedef void *queue_item;

size_t queue_size_bytes();

/** Create a new, empty queue. */
queue *queue_create();
/** Deallocate a queue. */
void queue_destroy(queue *queue);
void queue_close(queue* queue);

/** Add a single item to the queue, blocking. */
error_t queue_add_item(queue *queue, queue_item item);
/** Get the next item from the queue, blocking if `block`. */
queue_item queue_next_item(queue *queue, bool block);

#endif // LIBORAM_SHARDED_OHTABLE_QUEUE_H
