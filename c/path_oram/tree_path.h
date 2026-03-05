// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_PATH_ORAM_TREE_PATH_H
#define CDS_PATH_ORAM_TREE_PATH_H 1

#include "util/util.h"

typedef struct tree_path tree_path;
struct tree_path
{
    size_t length;
    u64 values[];
};

// Jasmin-exported layout functions (source of truth for struct tree_path layout)
extern size_t tree_path_length_offset_jazz(void);
extern size_t tree_path_values_offset_jazz(void);

tree_path *tree_path_create(u64 length);
void tree_path_destroy(tree_path *tp);
size_t tree_path_num_nodes(size_t num_levels);

/**
 * @brief Updates the tree path with node IDs from the root to the given leaf
 *
 * @param tp
 * @param leaf Leaf node identifier whose path is to be computed
 */
extern void tree_path_update_jazz(tree_path *tp, u64 leaf);

#ifdef IS_TEST
/**
 * @brief Test-only Jasmin function declarations for tree path operations.
 */
extern u64 tree_path_lower_bound_jazz(u64 val);
extern u64 tree_path_upper_bound_jazz(u64 val);
extern size_t tree_path_level_jazz(u64 val);

void private_tree_path_tests();
#endif // IS_TEST
#endif // CDS_PATH_ORAM_TREE_PATH_H
