// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_PATH_ORAM_POSITION_MAP_H
#define CDS_PATH_ORAM_POSITION_MAP_H 1

#include "util/util.h"
#include "path_oram.h"
#define POSITION_MAP_NOT_PRESENT UINT64_MAX
#define SCAN_THRESHOLD (1 << 14)

typedef struct position_map position_map;

position_map* oram_position_map_create_120G_16shards(entropy_func getentropy);
position_map* scan_position_map_create_120G_16shards(entropy_func getentropy);
position_map* oram_position_map_create_depth16(entropy_func getentropy);
position_map* scan_position_map_create_depth16(entropy_func getentropy);


void position_map_destroy(position_map *position_map);

size_t position_map_capacity(const position_map *position_map);

/**
 * @brief Number of position-map levels (including this level) needed to implement this position map. 
 * 
 * @param position_map 
 * @return size_t 
 */
size_t position_map_recursion_depth(const position_map* position_map);
const oram_statistics* position_map_oram_statistics(position_map* position_map);

#ifdef IS_TEST
/**
 * @brief Read the current position of a block and update it with a new one.
 *
 * @param position_map
 * @param block_id
 * @param position new position to assign to the block
 * @param prev_position pointer where the previous position will be stored
 * @return err_SUCCESS if successful
 * @return err_ORAM__ if ORAM operation failed
 */
extern error_t position_map_read_then_set_jazz(position_map *position_map, u64 block_id, u64 position, u64 *prev_position);

/**
 * @brief Get the position of a block
 *
 * @param position_map
 * @param block_id
 * @param position the result will be written here
 * @return `err_SUCCESS` if successful
 * @return `err_ORAM__` if ORAM operation failed
 */
error_t position_map_read_jazz(const position_map *position_map, u64 block_id, u64* position);

int private_position_map_tests();
#endif // IS_TEST
#endif // CDS_PATH_ORAM_POSITION_MAP_H
