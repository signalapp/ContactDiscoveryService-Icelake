// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <assert.h>
#include <stdio.h>

#include "path_oram/tree_path.h"
#include "util/util.h"
#include "util/tests.h"

int test_path(u64 leaf, size_t length,
              const u64 init_expected_path[length],
              const u64 update_expected_path[length])
{
    tree_path *path = tree_path_create(length);

    // check initialization
    assert(path->length == length);
    for (size_t i = 0; i < length; ++i)
    {
        TEST_ASSERT(init_expected_path[i] == path->values[i]);
    }

    // check path update
    tree_path_update_jazz(path, leaf);
    for (size_t i = 0; i < length; ++i)
    {
        TEST_ASSERT(update_expected_path[i] == path->values[i]);
    }
    tree_path_destroy(path);
    return err_SUCCESS;
}

int test_paths()
{
    u64 init_4[]  = {0, 1, 3, 7};
    u64 init_11[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023};
    u64 init_16[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767};
    
    u64 e1[] = {10, 9, 11, 7};
    test_path(10, 4, init_4, e1);

    u64 e2[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023};
    test_path(0, 11, init_11, e2);

    u64 e3[] = {2046, 2045, 2043, 2039, 2031, 2015, 1983, 1919, 1791, 1535, 1023};
    test_path(2046, 11, init_11, e3);

    u64 e4[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767};
    test_path(0, 16, init_16, e4);

    u64 e5[] = {124, 125, 123, 119, 111, 95, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767};
    test_path(124, 16, init_16, e5);

    u64 e6[] = {65534, 65533, 65531, 65527, 65519, 65503, 65471, 65407, 65279, 65023, 64511, 63487, 61439, 57343, 49151, 32767};
    test_path(65534, 16, init_16, e6);

    return err_SUCCESS;
}

void public_tree_path_tests()
{
    RUN_TEST(test_paths());
}

int main()
{

    private_tree_path_tests();
    public_tree_path_tests();

    return 0;
}