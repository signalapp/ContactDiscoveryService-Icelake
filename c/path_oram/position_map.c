// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include "position_map.h"
#include "path_oram.h"
#include "bucket.h"
#include "util/util.h"

#include <stdio.h>

// The most basic position map uses a linear scan to provide oblivious RAM security.
// We will want to use this for 2^14 or fewer entries, then use an ORAM
// backed position_map for more.
typedef struct
{
    size_t size;
    u32 *data;
} scan_position_map;


typedef struct
{
    size_t size;
    oram *oram;

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

static void oram_position_map_destroy(oram_position_map oram_position_map)
{
    oram_destroy(oram_position_map.oram);
}

static size_t oram_position_map_capacity(oram_position_map oram_position_map)
{
    return oram_position_map.size;
}

// scan implementation
static scan_position_map scan_position_map_create(size_t size, size_t num_positions, entropy_func getentropy)
{
    u32 *data;
    CHECK(data = calloc(size, sizeof(*data)));
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

static size_t scan_position_map_capacity(scan_position_map scan_position_map)
{
    return scan_position_map.size;
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


position_map *oram_position_map_create_120G_16shards(entropy_func getentropy)
{
    // Top level ORAM has 20 levels, 2^19 = 524288 positions, and 1.625*2^20 = 851968 block ids.
    size_t num_block_ids_in_domain = 851968;
    size_t num_positions_in_range = 524288;

    oram* oram = oram_create_120G_16shards_posmap(getentropy);
    size_t block_size = oram_block_size(oram); // 168
    size_t blocks_needed = num_block_ids_in_domain / (block_size * 2) + ((num_block_ids_in_domain % (block_size * 2) == 0) ? 0 : 1); // 2536

    TEST_LOG("oram_position_map domain size: %zu blocks: %zu", num_block_ids_in_domain, blocks_needed);

    // initialize position map with random data
    u32 *buf;
    CHECK(buf = calloc(block_size * 2, sizeof(*buf)));
    for (size_t i = 0; i < blocks_needed; ++i)
    {
        for (size_t j = 0; j < block_size * 2; ++j)
        {
            getentropy(buf + j, sizeof(*buf));
            buf[j] = buf[j] % num_positions_in_range;
        }
        // fprintf(stderr, "oram_position_map_create: putting block_id: %" PRIu64 "\n", i); // DEBUG
        oram_position_map_put(oram, i, (u64*)buf);
    }
    free(buf);

    size_t entries_per_block = block_size * 2;

    oram_position_map opm = {.oram = oram, .size = num_block_ids_in_domain, .entries_per_block = entries_per_block};
    prep_ct_div(
        opm.entries_per_block,
        &opm.entries_per_block_ct_m_prime,
        &opm.entries_per_block_ct_shift1,
        &opm.entries_per_block_ct_shift2);

    position_map *result;
    CHECK(result = calloc(1, sizeof(*result)));
    result->num_positions = num_positions_in_range;
    result->type = oram_map;
    result->impl.oram_position_map = opm;
    return result;
}

position_map *scan_position_map_create_120G_16shards(entropy_func getentropy)
{
    // Position map ORAM has 13 levels and we are not overloading it.
    // It requires 2536 blocks to hold its array of positions, and each of these needs
    // to be mapped to one of 4096 positions.
    // 8192 block IDs that need to be mapped to one of 8192 positions.
    size_t num_block_ids_in_domain = 2536;
    size_t num_positions_in_range = 4096;

    position_map *result;
    CHECK(result = calloc(1, sizeof(*result)));
    result->num_positions = num_positions_in_range;
    result->type = scan_map;
    result->impl.scan_position_map 
        = scan_position_map_create(num_block_ids_in_domain, num_positions_in_range, getentropy);
    return result;
}

position_map *oram_position_map_create_depth16(entropy_func getentropy)
{
    // Top level ORAM has 16 levels, 2^15 = 32768 positions, and 1.625*2^15 = 53248 block ids.
    size_t num_block_ids_in_domain = 53248;
    size_t num_positions_in_range = 32768;
    
    oram* oram = oram_create_depth16_posmap(getentropy);
    size_t block_size = oram_block_size(oram); // 168
    size_t blocks_needed = num_block_ids_in_domain / (block_size * 2) + ((num_block_ids_in_domain % (block_size * 2) == 0) ? 0 : 1); // 159

    TEST_LOG("oram_position_map domain size: %zu blocks: %zu", num_block_ids_in_domain, blocks_needed);
    
    // initialize position map with random data
    u32 *buf;
    CHECK(buf = calloc(block_size * 2, sizeof(*buf)));
    for (size_t i = 0; i < blocks_needed; ++i)
    {
        // TEST_LOG( "oram_position_map_create: putting block_id: %" PRIu64 "\n", i); // DEBUG
        for (size_t j = 0; j < block_size * 2; ++j)
        {
            getentropy(buf + j, sizeof(*buf));
            buf[j] = buf[j] % num_positions_in_range;
        }
        oram_position_map_put(oram, i, (u64*)buf);
    }
    free(buf);
    
    size_t entries_per_block = block_size * 2;
    oram_position_map opm = {
        .oram = oram,
        .size = num_block_ids_in_domain,
        .entries_per_block = entries_per_block
    };
    
    prep_ct_div(
        opm.entries_per_block,
        &opm.entries_per_block_ct_m_prime,
        &opm.entries_per_block_ct_shift1,
        &opm.entries_per_block_ct_shift2);
    
    position_map *result;
    CHECK(result = calloc(1, sizeof(*result)));
    result->num_positions = num_positions_in_range;
    result->type = oram_map;
    result->impl.oram_position_map = opm;
    
    return result;
}

position_map *scan_position_map_create_depth16(entropy_func getentropy)
{
    // Position map ORAM has 9 levels and we are not overloading it.
    // It requires 159 blocks to hold its array of positions, and each of these needs
    // to be mapped to one of 512 positions.
    size_t num_block_ids_in_domain = 159;
    size_t num_positions_in_range = 256;
    
    position_map *result;
    CHECK(result = calloc(1, sizeof(*result)));
    result->num_positions = num_positions_in_range;
    result->type = scan_map;
    result->impl.scan_position_map
        = scan_position_map_create(num_block_ids_in_domain, num_positions_in_range, getentropy);
    
    return result;
}
