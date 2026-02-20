// Copyright 2026 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "../util.h"
#include "../osort.h"
#include "../tests.h"

typedef struct {
    uint8_t flag;
    uint64_t partition;
    uint64_t key;
} sort_data;

bool cmp_partition_flag_key(const void *lhs, const void *rhs) {
    const sort_data *lhs_data = (sort_data*)lhs;
    const sort_data *rhs_data = (sort_data*)rhs;
    bool lhs_partition_bigger = lhs_data->partition > rhs_data->partition;
    bool lhs_partition_equal = lhs_data->partition == rhs_data->partition;
    bool lhs_flag_bigger = lhs_data->flag > rhs_data->flag;
    bool lhs_flag_equal = lhs_data->flag == rhs_data->flag;
    bool lhs_key_bigger = lhs_data->key > rhs_data->key;
    return lhs_partition_bigger
        | (lhs_partition_equal & lhs_flag_bigger)
        | (lhs_partition_equal & lhs_flag_equal & lhs_key_bigger);
}

#define NUM_SORT_RECORDS 100000
int test_osort() {
    // create an array of random records
    sort_data *records = calloc(NUM_SORT_RECORDS, sizeof(sort_data));
    CHECK(records);
    for(size_t i = 0; i < NUM_SORT_RECORDS; ++i) {
        records[i].key = i;
        records[i].flag = i%2;
        records[i].partition = 1 + i%3;
    }
    fprintf(stderr, "sizeof record: %zu\n", sizeof records[0]);

    osort_jazz((uint8_t*)records, 0, NUM_SORT_RECORDS);

    TEST_ASSERT(records[0].partition == 1);
    TEST_ASSERT(records[0].flag == 0);
    TEST_ASSERT(records[0].key == 0);
    for(size_t i = 1; i < NUM_SORT_RECORDS; ++i) {
        TEST_ASSERT(cmp_partition_flag_key(records + i, records + i - 1));
    }
    free(records);
    return 0;
}

int main(int argc, char** argv) {
  RUN_TEST(test_osort());
  return 0;
}
