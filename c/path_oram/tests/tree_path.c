// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <assert.h>
#include <stdio.h>

#include "path_oram/tree_path.h"
#include "util/util.h"
#include "util/tests.h"

int test_path(u64 leaf, u64 root, size_t expected_len, const u64 expected_path[expected_len])
{
    tree_path *path = tree_path_create(leaf, root);
    assert(path->length == expected_len);
    for (size_t i = 0; i < expected_len; ++i)
    {
        TEST_ASSERT(expected_path[i] == path->values[i]);
    }
    tree_path_destroy(path);
    return err_SUCCESS;
}

int test_paths()
{
    u64 e1[] = {10, 9, 11, 7};
    test_path(10, 7, 4, e1);

    u64 e2[] = {0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023};
    test_path(0, 1023, 11, e2);

    u64 e3[] = {2046, 2045, 2043, 2039, 2031, 2015, 1983, 1919, 1791, 1535, 1023};
    test_path(2046, 1023, 11, e3);

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
