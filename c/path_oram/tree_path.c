// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <strings.h>

#include "util/util.h"
#include "tree_path.h"

// Validate C layout matches Jasmin. Called at startup.
// Note: tree_path uses a flexible array member, so no sizeof validation.
__attribute__((constructor))
static void tree_path_validate_layout(void) {
    CHECK(offsetof(tree_path, length) == tree_path_length_offset_jazz());
    CHECK(offsetof(tree_path, values) == tree_path_values_offset_jazz());
}

// `tree_path` creates map from the integers from 0 to 2^L - 1 to the
// nodes of a binary tree of height L by performing an in-order traversal.
// Note that with this
//  * even numbers are the leaves. We call this level 0.
//  * numbers that are 1 mod 4 are on the next level - level 1
//  * numbers that are 3 mod 8 are on level 2
//  * and so on... numbers that are (2^k - 1) mod 2^(k+1) are on level k
//  * the root is the single node at level L-1: 2^(L-1)-1
//
// Based on that characterisation we will refer to 2^(k+1) as the `level_modulus`
// and we will refer to (2^k - 1) as the `level_residue` for level k. All items on
// level k are congruent to `level_residue` mod `level_modulus`.
//
// In path ORAM, memory blocks are assigned to leaves, i.e. even numbers, and
// a `tree_path` is a path to root from this leaf.

// We have two views of a node in a binary tree: in-order numbering or coordinates.
// `tree_coords` represents the location of a node in the tree by giving the level (leaves are level 0)
// and the offset in that level.
typedef struct
{
    size_t level;
    size_t offset;
} tree_coords;

tree_coords parent(tree_coords node)
{
    return (tree_coords){.offset = (node.offset >> 1), .level = node.level + 1};
}

u64 node_val(tree_coords node)
{
    u64 level_modulus = (1ULL << (node.level + 1));
    u64 level_residue = level_modulus / 2 - 1;

    return level_modulus * node.offset + level_residue;
}


tree_path *tree_path_create(u64 length)
{
    tree_path *t;
    CHECK(t = malloc(sizeof(tree_path) + sizeof(u64) * length));
    t->length = length;

    tree_path_update_jazz(t, 0);

    return t;
}

void tree_path_destroy(tree_path *tp)
{
    free(tp);
}

size_t tree_path_num_nodes(size_t num_levels)
{
    return ((size_t)1 << num_levels) - 1;
}

#ifdef IS_TEST
#include <stdio.h>
#include "util/util.h"
#include "util/tests.h"

int test_level()
{
    TEST_ASSERT(tree_path_level_jazz(1040678) == 0);

    TEST_ASSERT(tree_path_level_jazz(1) == 1);
    TEST_ASSERT(tree_path_level_jazz(10009) == 1);

    TEST_ASSERT(tree_path_level_jazz(3) == 2);
    TEST_ASSERT(tree_path_level_jazz(1003) == 2);

    TEST_ASSERT(tree_path_level_jazz(7) == 3);
    TEST_ASSERT(tree_path_level_jazz(10000007) == 3);

    TEST_ASSERT(tree_path_level_jazz(100000000255) == 8);

    for (size_t l = 0; l < 64; ++l)
    {
        u64 pow_2 = ((size_t)1) << l;
        TEST_ASSERT(tree_path_level_jazz(pow_2 - 1) == l);
    }

    return err_SUCCESS;
}

int test_descendent_range()
{
    u64 lb = tree_path_lower_bound_jazz(11);
    u64 ub = tree_path_upper_bound_jazz(11);
    TEST_ASSERT(lb == 8);
    TEST_ASSERT(ub == 14);
    return err_SUCCESS;
}

void private_tree_path_tests()
{
    RUN_TEST(test_level());
    RUN_TEST(test_descendent_range());
}
#endif
