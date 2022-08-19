// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdlib.h>
#include <strings.h>

#include "util/util.h"
#include "tree_path.h"

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

static size_t level(u64 n)
{
    return n == UINT64_MAX ? 0 : __builtin_ffsll(~n) - 1;
}

tree_coords coords_for_val(u64 val)
{
    size_t l = level(val);
    u64 level_modulus = (1ULL << (l + 1));
    u64 level_residue = level_modulus / 2 - 1;
    size_t offset = (val - level_residue) / level_modulus;
    tree_coords coords = {.level = l, .offset = offset};
    return coords;
}

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

tree_path *tree_path_create(u64 leaf, u64 root)
{
    size_t length = level(root) + 1;
    tree_path *t;
    CHECK(t = malloc(sizeof(tree_path) + sizeof(u64) * length));
    t->length = length;

    tree_path_update(t, leaf);

    return t;
}

void tree_path_update(tree_path *t, u64 leaf)
{
    size_t root_level = t->length - 1;
    u64 root = (1UL << root_level) - 1;

    // leaves are even numbers
    CHECK(leaf % 2 == 0);

    // root must be 2^(root_level+1) - 1 and leaf must
    // be <= 2*root, otherwise there may be no path to it.
    CHECK(leaf <= 2 * root);

    tree_coords coords = coords_for_val(leaf);

    t->values[0] = leaf;
    while (coords.level < root_level)
    {
        coords = parent(coords);
        t->values[coords.level] = node_val(coords);
    }
}

void tree_path_destroy(tree_path *tp)
{
    free(tp);
}

size_t tree_path_num_nodes(size_t num_levels)
{
    return ((size_t)1 << num_levels) - 1;
}

u64 tree_path_lower_bound(u64 val)
{
    size_t l = level(val);
    u64 step = (1ULL << l) - 1;
    return val - step;
}

u64 tree_path_upper_bound(u64 val)
{
    size_t l = level(val);
    u64 step = (1ULL << l) - 1;
    return val + step;
}

size_t tree_path_level(u64 val) {
    return level(val);
}

#ifdef IS_TEST
#include <stdio.h>
#include "util/util.h"
#include "util/tests.h"

int test_level()
{
    TEST_ASSERT(level(1040678) == 0);

    TEST_ASSERT(level(1) == 1);
    TEST_ASSERT(level(10009) == 1);

    TEST_ASSERT(level(3) == 2);
    TEST_ASSERT(level(1003) == 2);

    TEST_ASSERT(level(7) == 3);
    TEST_ASSERT(level(10000007) == 3);

    TEST_ASSERT(level(100000000255) == 8);

    for (size_t l = 0; l < 64; ++l)
    {
        u64 pow_2 = ((size_t)1) << l;
        TEST_ASSERT(level(pow_2 - 1) == l);
    }

    return err_SUCCESS;
}

int test_coords_from_val()
{
    tree_coords coords = coords_for_val(0);
    TEST_ASSERT(coords.level == 0);
    TEST_ASSERT(coords.offset == 0);

    coords = coords_for_val(1);
    TEST_ASSERT(coords.level == 1);
    TEST_ASSERT(coords.offset == 0);

    coords = coords_for_val(2);
    TEST_ASSERT(coords.level == 0);
    TEST_ASSERT(coords.offset == 1);

    coords = coords_for_val(3);
    TEST_ASSERT(coords.level == 2);
    TEST_ASSERT(coords.offset == 0);

    coords = coords_for_val(7);
    TEST_ASSERT(coords.level == 3);
    TEST_ASSERT(coords.offset == 0);

    coords = coords_for_val(23);
    TEST_ASSERT(coords.level == 3);
    TEST_ASSERT(coords.offset == 1);

    coords = coords_for_val(1087);
    TEST_ASSERT(coords.level == 6);
    TEST_ASSERT(coords.offset == 8);
    return err_SUCCESS;
}

int test_val_from_coords()
{
    u64 val = node_val((tree_coords){.level = 0, .offset = 0});
    TEST_ASSERT(val == 0);

    val = node_val((tree_coords){.level = 2, .offset = 1});
    TEST_ASSERT(val == 11);
    return 0;
}

int test_val_coords_roundtrip()
{
    for (size_t i = 0; i < 100000; ++i)
    {
        tree_coords coords = coords_for_val(i);
        u64 val = node_val(coords);
        TEST_ASSERT(val == i);
    }
    return err_SUCCESS;
}

int test_descendent_range()
{
    u64 lb = tree_path_lower_bound(11);
    u64 ub = tree_path_upper_bound(11);
    TEST_ASSERT(lb == 8);
    TEST_ASSERT(ub == 14);
    return err_SUCCESS;
}

void private_tree_path_tests()
{
    RUN_TEST(test_level());
    RUN_TEST(test_coords_from_val());
    RUN_TEST(test_val_from_coords());
    RUN_TEST(test_val_coords_roundtrip());
    RUN_TEST(test_descendent_range());
}
#endif
