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
    oram *oram = oram_create_depth16(getentropy);

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

int main(int argc, char *argv[])
{
    run_path_oram_tests();
    RUN_TEST(get_put_repeat());
    return 0;
}
