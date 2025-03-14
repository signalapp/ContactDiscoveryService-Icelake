// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_PATH_ORAM_POSITION_MAP_H
#define CDS_PATH_ORAM_POSITION_MAP_H 1

#include "util/util.h"
#include "path_oram.h"
#define POSITION_MAP_NOT_PRESENT UINT64_MAX
#define SCAN_THRESHOLD (1 << 14)

typedef struct position_map position_map;

/**
 * @brief The `position_map` is used internally by an ORAM to keep track of the current physical
 * location of ORAM blocks.
 *
 * @param num_blocks size of the domain of the map - the number of blocks in an ORAM
 * @param num_positions size of the range of the map - number of leaves in the bucket store
 * @param overflow_stash_size size of overflow stash, in blocks, to be used by any ORAM built to back this position map.
 * @return position_map*
 */
position_map *position_map_create(size_t num_blocks, size_t num_positions, size_t overflow_stash_size, entropy_func getentropy);
void position_map_destroy(position_map *position_map);

size_t position_map_capacity(const position_map *position_map);

/**
 * @brief Sets the position of a block and returns the previous position
 *
 * @param position_map
 * @param block_id ID of block of interest
 * @param position new position for the block
 * @param prev_position Required, will hold the previous position for this block on return
 * @return err_SUCCESS if successful
 * @return err_ORAM__ if ORAM operation failed
 */
error_t position_map_read_then_set(position_map *position_map, u64 block_id, u64 position, u64 *prev_position);

/**
 * @brief Number of position-map levels (including this level) needed to implement this position map. 
 * 
 * @param position_map 
 * @return size_t 
 */
size_t position_map_recursion_depth(const position_map* position_map);
const oram_statistics* position_map_oram_statistics(position_map* position_map);

/**
 * @brief Compute the size in bytes needed to hold a position map with a given number of positions.
 * 
 * @param num_blocks 
 * @param stash_overflow_size
 * @return size_t 
 */
size_t position_map_size_bytes(size_t num_blocks, size_t stash_overflow_size);

#ifdef IS_TEST
int private_position_map_tests();

/**
 * @brief Get the position of a block
 *
 * @param position_map
 * @param block_id
 * @param position the result will be written here
 * @return err_SUCCESS if successful
 * @return err_ORAM__ if ORAM operation failed
 */
error_t position_map_get(const position_map *position_map, u64 block_id, u64* position);

#endif // IS_TEST
#endif // CDS_PATH_ORAM_POSITION_MAP_H
