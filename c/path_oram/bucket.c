// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "tree_path.h"
#include "bucket.h"
#include "util/util.h"

// struct bucket_store layout must match Jasmin definitions in bucket.jinc.
// Jasmin is the source of truth; validated at runtime by bucket_store_validate_layout().
struct bucket_store
{
    size_t size_bytes;
    u8 *data;
};

// Validate C layout matches Jasmin. Called at startup.
__attribute__((constructor))
static void bucket_store_validate_layout(void) {
    CHECK(offsetof(bucket_store, size_bytes) == bucket_store_size_bytes_offset_jazz());
    CHECK(offsetof(bucket_store, data) == bucket_store_data_offset_jazz());
    CHECK(sizeof(bucket_store) == bucket_store_sizeof_jazz());
}

// Create a path ORAM bucket store with capacity for a tree with `num_levels` levels,
// i.e. 2^num_levels - 1 tree nodes and 2^(num_levels - 1) leaf nodes/pathORAM positions.
bucket_store *bucket_store_create(size_t num_levels)
{
    size_t num_buckets = tree_path_num_nodes(num_levels);
    size_t size_bytes = num_buckets * ENCRYPTED_BUCKET_SIZE;

    u8 *data;
    CHECK(data = malloc(size_bytes));
    bucket_store *bucket_store;
    CHECK(bucket_store = calloc(1, sizeof(*bucket_store)));

    memset(data, 255, size_bytes);
    bucket_store->data = data;
    bucket_store->size_bytes = size_bytes;
    return bucket_store;
}
void bucket_store_destroy(bucket_store *bucket_store)
{
    // Acceptable if: not executed in oram_access
    if (bucket_store)
    {
        free(bucket_store->data);
        free(bucket_store);
    }
}

void bucket_store_clear(bucket_store *bucket_store)
{
    memset(bucket_store->data, 255, bucket_store->size_bytes);
}

size_t bucket_store_block_data_size(bucket_store *bucket_store)
{
    return BLOCK_DATA_SIZE_QWORDS; // 168
}

bool block_is_empty(block block)
{
    return block.id == EMPTY_BLOCK_ID;
}

#ifdef IS_TEST
#include <stdio.h>
#include <time.h>
#include "util/tests.h"

// Planning to add tests here when using pruned trees and variable branching
void private_bucket_store_tests()
{
    printf("TEST private bucket store functions");
}
#endif
