// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdio.h>
#include <sys/random.h>

#include "ohtable/ohtable.h"
#include "path_oram/path_oram.h"
#include "util/util.h"
#include "util/tests.h"

typedef struct record
{
    u64 key;
    u64 a;
    u64 b;
    u64 c;
    u64 d;
    u64 e;
    u64 f;
} record;

int put_get_cycle_works()
{
    ohtable *ohtable = ohtable_create(5000000, 7, TEST_STASH_SIZE, getentropy);

    record r0 = {.key = 1, .a = 2, .b = 3, .c = 4, .d = 5, .e = 6};
    RETURN_IF_ERROR(ohtable_put(ohtable, (const u64 *)&r0));

    record recovered;
    RETURN_IF_ERROR(ohtable_get(ohtable, r0.key, (u64 *)&recovered));
    TEST_ASSERT(r0.key == recovered.key);
    TEST_ASSERT(r0.a == recovered.a);
    TEST_ASSERT(r0.b == recovered.b);
    TEST_ASSERT(r0.c == recovered.c);
    TEST_ASSERT(r0.d == recovered.d);
    TEST_ASSERT(r0.e == recovered.e);
    ohtable_destroy(ohtable);
    return err_SUCCESS;
}

int loaded_table()
{
    size_t cap = 30000;
    ohtable *ohtable = ohtable_create(cap, 7, TEST_STASH_SIZE, getentropy);

    for (u64 i = 0; i < 0.8 * cap; ++i)
    {
        record r0 = {.key = i, .a = 2 * i, .b = 3 * i, .c = 4 * i, .d = 5 * i, .e = 6 * i};
        RETURN_IF_ERROR(ohtable_put(ohtable, (const u64 *)&r0));
    }

    for (size_t i = 0; i < 0.8 * cap; ++i)
    {
        record recovered;
        RETURN_IF_ERROR(ohtable_get(ohtable, i, (u64 *)&recovered));

        TEST_ASSERT(i == recovered.key);
        TEST_ASSERT(2 * i == recovered.a);
        TEST_ASSERT(3 * i == recovered.b);
        TEST_ASSERT(4 * i == recovered.c);
        TEST_ASSERT(5 * i == recovered.d);
        TEST_ASSERT(6 * i == recovered.e);
    }

    for (u64 i = 0.8 * cap; i < cap; ++i)
    {
        record recovered;
        RETURN_IF_ERROR(ohtable_get(ohtable, i, (u64 *)&recovered));
        TEST_ASSERT(UINT64_MAX == recovered.key);
        TEST_ASSERT(UINT64_MAX == recovered.a);
        TEST_ASSERT(UINT64_MAX == recovered.b);
        TEST_ASSERT(UINT64_MAX == recovered.c);
        TEST_ASSERT(UINT64_MAX == recovered.d);
        TEST_ASSERT(UINT64_MAX == recovered.e);
    }
    ohtable_destroy(ohtable);
    return err_SUCCESS;
}

int test_ohtable_stats_and_clear() {
    size_t cap = 30000;
    ohtable *ohtable = ohtable_create(cap, 7, TEST_STASH_SIZE, getentropy);

    for (u64 i = 0; i < 1000; ++i)
    {
        record r0 = {.key = i, .a = 2 * i, .b = 3 * i, .c = 4 * i, .d = 5 * i, .e = 6 * i};
        RETURN_IF_ERROR(ohtable_put(ohtable, (const u64 *)&r0));
    }

    TEST_ASSERT(ohtable_capacity(ohtable) >= cap);
    TEST_ASSERT(ohtable_num_items(ohtable) == 1000);

    ohtable_clear(ohtable);
    TEST_ASSERT(ohtable_capacity(ohtable) >= cap);
    TEST_ASSERT(ohtable_num_items(ohtable) == 0);

    ohtable_destroy(ohtable);
    return err_SUCCESS;
}

error_t test_full_table() {
    size_t cap = 10000;
    ohtable *ohtable = ohtable_create(cap, 7, TEST_STASH_SIZE, getentropy);
    size_t capacity = ohtable_capacity(ohtable);

    for (u64 i = 0; i < 0.98*capacity; ++i)
    {
        record r0 = {.key = i, .a = 2 * i, .b = 3 * i, .c = 4 * i, .d = 5 * i, .e = 6 * i};
        RETURN_IF_ERROR(ohtable_put(ohtable, (const u64 *)&r0));
    }

    record r = {.key = 0.98 * capacity};
    error_t err = ohtable_put(ohtable, (const u64*)&r);
    TEST_ASSERT(err == err_OHTABLE__TABLE_FULL);

    ohtable_destroy(ohtable);
    return err_SUCCESS;
}

void public_ohtable_tests()
{
    RUN_TEST(put_get_cycle_works());
    RUN_TEST(loaded_table());
    RUN_TEST(test_ohtable_stats_and_clear());
    RUN_TEST(test_full_table());
}

int main()
{
    private_ohtable_tests();
    public_ohtable_tests();
    return 0;
}
