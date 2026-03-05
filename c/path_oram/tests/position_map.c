// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#include "path_oram/position_map.h"
#include "path_oram/bucket.h"
#include "util/util.h"
#include "util/tests.h"

int test_position_map_lifecycle()
{
    position_map *pm = oram_position_map_create_depth16(getentropy);
    TEST_ASSERT(pm != 0);
    position_map_destroy(pm);

    return err_SUCCESS;
}

static int cmpu64(const void *pa, const void *pb)
{
    u64 a = *(u64 *)pa;
    u64 b = *(u64 *)pb;
    return (a > b) - (b > a);
}

int test_position_map_initial_data()
{
    size_t size = 32768;
    position_map *pm = oram_position_map_create_depth16(getentropy);
    u64 *data = calloc(size, sizeof(*data));
    for (size_t i = 0; i < size; ++i)
    {
        RETURN_IF_ERROR(position_map_read_jazz(pm, i, data + i));
    }

    qsort(data, size, sizeof(*data), cmpu64);

    size_t num_repeat_histogram[20];
    memset(num_repeat_histogram, 0, 20 * sizeof(*num_repeat_histogram));

    u64 curr_item = data[0];
    size_t run_length = 1;
    for (size_t i = 0; i < size; ++i)
    {
        if (curr_item == data[i])
        {
            ++run_length;
        }
        else
        {
            if (run_length >= 20)
            {
                fprintf(stderr, "WARNING encountered a long run length: %" PRIu64 " occured %zu times\n", curr_item, run_length);
                run_length = 19;
            }
            num_repeat_histogram[run_length]++;
            run_length = 1;
            curr_item = data[i];
        }
    }
    free(data);
    position_map_destroy(pm);

    return err_SUCCESS;
}

int test_position_map_put_get()
{
    position_map *pm = oram_position_map_create_depth16(getentropy);

    u64 prev;
    RETURN_IF_ERROR(position_map_read_then_set_jazz(pm, 1234, 4321, &prev));
    u64 result;
    RETURN_IF_ERROR(position_map_read_jazz(pm, 1234, &result));

    TEST_ASSERT(result == 4321);

    position_map_destroy(pm);

    return err_SUCCESS;
}

int test_position_map_put_get_repeat()
{
    size_t num_positions = 32768;
    position_map *pm = oram_position_map_create_depth16(getentropy);

    for (size_t i = 0; i < num_positions; ++i)
    {
        u64 prev;
        RETURN_IF_ERROR(position_map_read_then_set_jazz(pm, i, i, &prev));
    }

    for (size_t i = 0; i < num_positions; ++i)
    {
        u64 result;
        RETURN_IF_ERROR(position_map_read_jazz(pm, i, &result));
        TEST_ASSERT(result == i);
    }
    for (size_t i = 0; i < num_positions; ++i)
    {
        u64 old_pos = 0;
        RETURN_IF_ERROR(position_map_read_then_set_jazz(pm, i, num_positions - i, &old_pos));
        TEST_ASSERT(old_pos == i);
    }

    for (size_t i = 0; i < num_positions; ++i)
    {
        u64 result;
        RETURN_IF_ERROR(position_map_read_jazz(pm, i, &result));
        TEST_ASSERT(result == num_positions - i);
    }
    position_map_destroy(pm);

    return err_SUCCESS;
}
void public_position_map_tests()
{
    RUN_TEST(test_position_map_lifecycle());
    RUN_TEST(test_position_map_initial_data());
    RUN_TEST(test_position_map_put_get());
    RUN_TEST(test_position_map_put_get_repeat());
}

int main()
{
    public_position_map_tests();

    return 0;
}
