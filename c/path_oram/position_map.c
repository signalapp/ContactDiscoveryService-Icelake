// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include "position_map.h"
#include "path_oram.h"
#include "bucket.h"

#include <stdio.h>

// The most basic position map uses a linear scan to provide oblivious RAM security.
// We will want to use this for 2^14 or fewer entries, then use an ORAM
// backed position_map for more.
typedef struct
{
    size_t size;
    u64 *data;
} scan_position_map;


typedef struct
{
    size_t size;
    oram *oram;
    u64 base_block_id;
    u64 *access_buf;

    // constant-time mod precomputations
    u64 entries_per_block;
    u64 entries_per_block_ct_m_prime;
    size_t entries_per_block_ct_shift1;
    size_t entries_per_block_ct_shift2;
} oram_position_map;

typedef enum
{
    scan_map,
    oram_map
} position_map_type;

struct position_map
{
    position_map_type type;
    size_t num_positions;
    union
    {
        scan_position_map scan_position_map;
        oram_position_map oram_position_map;
    } impl;
};

// oram implementation
static oram_position_map oram_position_map_create(size_t num_blocks, size_t num_positions, size_t overflow_stash_size, entropy_func getentropy)
{
    CHECK(num_positions <= num_blocks);
    // oram capacity is measured in u64s
    oram *oram = oram_create(num_blocks, overflow_stash_size, getentropy);
    size_t block_size = oram_block_size(oram);
    size_t blocks_needed = num_blocks / block_size + ((num_blocks % block_size == 0) ? 0 : 1);
    u64 base_block_id = oram_allocate_contiguous(oram, blocks_needed);
    TEST_LOG("oram_position_map size: %zu blocks: %zu", num_blocks, blocks_needed);

    // initialize position map with random data
    u64 *buf;
    CHECK(buf = calloc(block_size, sizeof(*buf)));
    for (size_t i = 0; i < blocks_needed; ++i)
    {
        for (size_t j = 0; j < block_size; ++j)
        {
            getentropy(buf + j, sizeof(*buf));
            buf[j] = buf[j] % num_positions;
        }
        oram_put(oram, base_block_id + i, buf);
    }

    size_t entries_per_block = block_size;

    oram_position_map result = {.base_block_id = base_block_id, .oram = oram, .size = num_blocks, .entries_per_block = entries_per_block};
    prep_ct_div(
        result.entries_per_block, 
        &result.entries_per_block_ct_m_prime, 
        &result.entries_per_block_ct_shift1, 
        &result.entries_per_block_ct_shift2);
    result.access_buf = buf;
    return result;
}

static void oram_position_map_destroy(oram_position_map oram_position_map)
{
    oram_destroy(oram_position_map.oram);
    free(oram_position_map.access_buf);
}

static u64 block_id_for_index(const oram_position_map *oram_position_map, u64 index)
{
    size_t entries_per_block = oram_block_size(oram_position_map->oram);
    size_t offset = ct_div(index, 
        entries_per_block, 
        oram_position_map->entries_per_block_ct_m_prime,
        oram_position_map->entries_per_block_ct_shift1,
        oram_position_map->entries_per_block_ct_shift2);
    return oram_position_map->base_block_id + offset;
}

static error_t oram_position_map_set(oram_position_map *oram_position_map, u64 block_id, u64 position, u64 *prev_position)
{
    CHECK(prev_position!= NULL);
    size_t idx_in_block = ct_mod(
        block_id, 
        oram_position_map->entries_per_block, 
        oram_position_map->entries_per_block_ct_m_prime, 
        oram_position_map->entries_per_block_ct_shift1, 
        oram_position_map->entries_per_block_ct_shift2);
    size_t len_to_put = 1;
    RETURN_IF_ERROR(oram_put_partial(oram_position_map->oram, block_id_for_index(oram_position_map, block_id), idx_in_block, len_to_put, &position, prev_position));

    return err_SUCCESS;
}

static size_t oram_position_map_capacity(oram_position_map oram_position_map)
{
    return oram_position_map.size;
}

// scan implementation
static scan_position_map scan_position_map_create(size_t size, size_t num_positions, entropy_func getentropy)
{
    u64 *data;
    CHECK(data = calloc(size, sizeof(*data)));
    TEST_LOG("scan_position_map size: %zu", size);
    scan_position_map scan_position_map = {.size = size, .data = data};

    for (size_t i = 0; i < size; ++i)
    {
        getentropy(data + i, sizeof(*data));
        data[i] = data[i] % num_positions;
    }
    return scan_position_map;
}

static void scan_position_map_destroy(scan_position_map scan_position_map)
{
    free(scan_position_map.data);
}

static error_t scan_position_map_set(scan_position_map *scan_position_map, u64 block_id, u64 position, u64 *prev_position)
{
    CHECK(block_id < scan_position_map->size);
    u64 tmp;
    prev_position = prev_position ? prev_position : &tmp;
    *prev_position = position;
    // linear scan of array so that every access looks the same.
    for (size_t i = 0; i < scan_position_map->size; ++i)
    {
        bool cond = (i == block_id);
        cond_obv_swap_u64(cond, prev_position, scan_position_map->data + i);
    }
    return err_SUCCESS;
}

static size_t scan_position_map_capacity(scan_position_map scan_position_map)
{
    return scan_position_map.size;
}

// position_map public interface
position_map *position_map_create(size_t size, size_t num_positions, size_t overflow_stash_size, entropy_func getentropy)
{
    position_map *result;
    CHECK(result = calloc(1, sizeof(*result)));
    result->num_positions = num_positions;
    // Acceptable if: this is not executed in an oram_access
    if (size > SCAN_THRESHOLD)
    {
        result->type = oram_map;
        result->impl.oram_position_map = oram_position_map_create(size, num_positions, overflow_stash_size, getentropy);
    }
    else
    {
        result->type = scan_map;
        result->impl.scan_position_map = scan_position_map_create(size, num_positions, getentropy);
    }
    return result;
}

void position_map_destroy(position_map *position_map)
{
    // Acceptable if: this is not executed in an oram_access
    if (position_map)
    {
        switch (position_map->type)
        {
        case scan_map:
            scan_position_map_destroy(position_map->impl.scan_position_map);
            break;
        case oram_map:
            oram_position_map_destroy(position_map->impl.oram_position_map);
            break;
        default:
            CHECK(false);
            break;
        }
        free(position_map);
    }
}

error_t position_map_read_then_set(position_map *position_map, u64 block_id, u64 position, u64 *prev_position)
{
    // Acceptable switch: executed identically in each oram_access
    switch (position_map->type)
    {
    case scan_map:
        return scan_position_map_set(&position_map->impl.scan_position_map, block_id, position, prev_position);
    case oram_map:
        return oram_position_map_set(&position_map->impl.oram_position_map, block_id, position, prev_position);
    default:
        CHECK(false);
        break;
    }
}

size_t position_map_capacity(const position_map *position_map)
{
    u64 result = 0;
    // Acceptable switch: not executed in an oram_access
    switch (position_map->type)
    {
    case scan_map:
        result = scan_position_map_capacity(position_map->impl.scan_position_map);
        break;
    case oram_map:
        result = oram_position_map_capacity(position_map->impl.oram_position_map);
        break;
    default:
        CHECK(false);
        break;
    }
    return result;
}

size_t position_map_recursion_depth(const position_map* position_map) {

    const oram_statistics* oram_stats = NULL;
    // Acceptable switch: not executed in an oram_access
    switch (position_map->type)
    {
    case scan_map:
        return 1;
    case oram_map:
        oram_stats = oram_report_statistics(position_map->impl.oram_position_map.oram);
        return 1 + oram_stats->recursion_depth;
    default:
        CHECK(false);
    }
    return 0;
}

const oram_statistics* position_map_oram_statistics(position_map* position_map) {
    // Acceptable switch: not executed in oram_access
    switch (position_map->type)
    {
    case scan_map:
        return NULL;
    case oram_map:
        return oram_report_statistics(position_map->impl.oram_position_map.oram);
    default:
        CHECK(false);
    }
    return NULL;
}


size_t position_map_size_bytes(size_t num_blocks, size_t stash_overflow_size) {
    // Acceptable if: this is not executed in an oram_access
    if(num_blocks > SCAN_THRESHOLD) {
        size_t blocks_needed = num_blocks / BLOCK_DATA_SIZE_QWORDS + 1;
        size_t num_levels = floor_log2(blocks_needed);
        return oram_size_bytes(num_levels, blocks_needed, stash_overflow_size) + sizeof(position_map);
    }
    
    return num_blocks * sizeof(u64) + sizeof(position_map); 

}

#ifdef IS_TEST


static error_t oram_position_map_get(const oram_position_map *oram_position_map, u64 block_id, u64* position)
{
    size_t idx_in_block = ct_mod(
        block_id, 
        oram_position_map->entries_per_block, 
        oram_position_map->entries_per_block_ct_m_prime, 
        oram_position_map->entries_per_block_ct_shift1, 
        oram_position_map->entries_per_block_ct_shift2);
    size_t len_to_get = 1;

    RETURN_IF_ERROR(oram_get_partial(oram_position_map->oram, block_id_for_index(oram_position_map, block_id), idx_in_block, len_to_get, position));
    
    return err_SUCCESS;
}


static error_t scan_position_map_get(const scan_position_map *scan_position_map, u64 block_id, u64* position)
{  
    CHECK(block_id < scan_position_map->size);
    // linear scan of array so that every access looks the same.
    for (size_t i = 0; i < scan_position_map->size; ++i)
    {
        bool cond = (i == block_id);
        cond_obv_cpy_u64(cond, position, scan_position_map->data + i);
    }
    return err_SUCCESS;
}


error_t position_map_get(const position_map *position_map, u64 block_id, u64* position)
{
    // Acceptable switch: executed identically in each oram_access
    switch (position_map->type)
    {
    case scan_map:
        RETURN_IF_ERROR(scan_position_map_get(&position_map->impl.scan_position_map, block_id, position));
        break;
    case oram_map:
        RETURN_IF_ERROR(oram_position_map_get(&position_map->impl.oram_position_map, block_id, position));
        break;
    default:
        CHECK(false);
        break;
    }
    return err_SUCCESS;
}

#endif // IS_TEST