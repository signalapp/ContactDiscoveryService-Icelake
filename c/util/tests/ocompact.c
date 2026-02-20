// Copyright 2026 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "../util.h"
#include "../ocompact.h"
#include "../tests.h"

typedef struct {
    uint8_t flag;
    uint64_t partition;
    uint64_t key;
} sort_data;

bool flag_is_set(const void *record) {
    const sort_data *sd = record;
    return sd->flag != 0;
}

#define NUM_COMPACT_RECORDS 40
int test_ocompact() {
    // create an array of random records
    sort_data records[NUM_COMPACT_RECORDS];
    for(size_t i = 0; i < NUM_COMPACT_RECORDS; ++i) {
        records[i].key = i;
        records[i].flag = i%2;
        records[i].partition = 1 + i%3;
    }
    fprintf(stderr, "sizeof record: %zu\n", sizeof records[0]);

    size_t* distances = 0;
    CHECK(distances = calloc(NUM_COMPACT_RECORDS, sizeof distances[0]));
    ocompact_jazz((uint8_t*)records, NUM_COMPACT_RECORDS, distances);
    free(distances);
    for(size_t i = 0; i < NUM_COMPACT_RECORDS/2; ++i) {
        TEST_ASSERT(records[i].flag == 1);
        if(i > 0) {
            TEST_ASSERT(records[i-1].key + 2 == records[i].key);
        }
    }

    for(size_t i = NUM_COMPACT_RECORDS/2; i < NUM_COMPACT_RECORDS; ++i) {
        TEST_ASSERT(records[i].flag == 0);
    }
    return 0;
}

int main(int argc, char** argv) {
  RUN_TEST(test_ocompact());
  return 0;
}
