// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/random.h>
#include "path_oram/path_oram.h"
#include "path_oram/bucket.h"
#include "util/util.h"
#include "util/tests.h"

int get_put_repeat()
{
    size_t capacity = 1 << 22;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);

    oram_allocate_contiguous(oram, 1330);
    oram_allocate_block(oram);

    for (size_t b = 0; b < 1331; ++b)
    {
        u64 buf[BLOCK_DATA_SIZE_QWORDS];
        for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
        {
            buf[i] = b * BLOCK_DATA_SIZE_QWORDS + i;
        }
        RETURN_IF_ERROR(oram_put(oram, b, buf));
    }

    for (size_t b = 0; b < 1331; ++b)
    {
        u64 buf[BLOCK_DATA_SIZE_QWORDS];
        RETURN_IF_ERROR(oram_get(oram, b, buf));
        for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
        {
            TEST_ASSERT(buf[i] == b * BLOCK_DATA_SIZE_QWORDS + i);
        }
    }

    oram_destroy(oram);
    return err_SUCCESS;
}

int test_oram_clear()
{
    size_t capacity = 1 << 22;
    oram *oram = oram_create(capacity, TEST_STASH_SIZE, getentropy);

    oram_allocate_contiguous(oram, 1330);
    oram_allocate_block(oram);

    for (size_t b = 0; b < 1331; ++b)
    {
        u64 buf[BLOCK_DATA_SIZE_QWORDS];
        for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
        {
            buf[i] = b * BLOCK_DATA_SIZE_QWORDS + i;
        }
        RETURN_IF_ERROR(oram_put(oram, b, buf));
    }

    // now clear the ORAM
    oram_clear(oram);

    for (size_t b = 0; b < 1331; ++b)
    {
        u64 buf[BLOCK_DATA_SIZE_QWORDS];
        memset(buf, 0, BLOCK_DATA_SIZE_BYTES);
        TEST_ASSERT(err_ORAM__ACCESS_UNALLOCATED_BLOCK == oram_get(oram, b, buf));

        for (size_t i = 0; i < BLOCK_DATA_SIZE_QWORDS; ++i)
        {
            TEST_ASSERT(buf[i] == 0);
        }
    }

    oram_destroy(oram);
    return err_SUCCESS;
}

error_t test_create_for_avail_mem() {

    // 2+ GiB
    size_t avail_bytes = (2ul << 30) + (1ul << 25);
    oram* oram = oram_create_for_available_mem(avail_bytes, 1.5, TEST_STASH_SIZE, getentropy);
    TEST_ASSERT(oram != NULL);
    TEST_ASSERT(oram_capacity_blocks(oram) == (1ul << 18) * 1.5);
    oram_destroy(oram);


    // 2- GiB
    avail_bytes = (2ul << 30) - (1ul << 25);
    oram = oram_create_for_available_mem(avail_bytes, 1.5, TEST_STASH_SIZE, getentropy);
    TEST_ASSERT(oram != NULL);
    TEST_ASSERT(oram_capacity_blocks(oram) == (1ul << 17) * 1.5);
    oram_destroy(oram);
    return err_SUCCESS;
}

int main(int argc, char *argv[])
{
    run_path_oram_tests();
    RUN_TEST(get_put_repeat());
    RUN_TEST(test_oram_clear());
    RUN_TEST(test_create_for_avail_mem());
    return 0;
}
