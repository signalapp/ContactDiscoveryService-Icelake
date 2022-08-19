// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <inttypes.h>
#include <stdlib.h>
#include "util/util.h"

#include "testhost/ohtable_stats.h"

static uint64_t read_stat(struct org_signal_cdsi_shard_statistics_t pb_stats, const char* name) {
    
    for(int i = 0; i < pb_stats.values.length; ++i) {
        if(strcmp(name, pb_stats.values.items_p[i].name_p) == 0) {
            return pb_stats.values.items_p[i].value;
        }
    }
    return UINT64_MAX;
}

static void copy_pb_shard_stats_to_struct(struct org_signal_cdsi_shard_statistics_t pb_stats, ohtable_statistics* table_stats) {
    table_stats->capacity = read_stat(pb_stats, "capacity");
    table_stats->max_stash_overflow_count = read_stat(pb_stats, "max_stash_overflow_count");
    table_stats->max_trace_length = read_stat(pb_stats, "max_trace_length");
    table_stats->num_items = read_stat(pb_stats, "num_items");
    table_stats->oram_access_count = read_stat(pb_stats, "oram_access_count");
    table_stats->oram_recursion_depth = read_stat(pb_stats, "oram_recursion_depth");
    table_stats->posmap_max_stash_overflow_count = read_stat(pb_stats, "posmap_max_stash_overflow_count");
    table_stats->posmap_stash_overflow_count = read_stat(pb_stats, "posmap_stash_overflow_count");
    table_stats->posmap_stash_overflow_ema10k = (double)read_stat(pb_stats, "posmap_stash_overflow_ema10k")/10000.;
    table_stats->posmap_sum_stash_overflow_count = read_stat(pb_stats, "posmap_sum_stash_overflow_count");
    table_stats->stash_overflow_count = read_stat(pb_stats, "stash_overflow_count");
    table_stats->stash_overflow_ema10k = (double)read_stat(pb_stats, "stash_overflow_ema10k")/10000.;
    table_stats->sum_stash_overflow_count = read_stat(pb_stats, "sum_stash_overflow_count");
    table_stats->total_displacement = read_stat(pb_stats, "total_displacement");
}

error_t decode_statistics(size_t pbsize, uint8_t* pb, ohtable_statistics* stats, size_t num_shards) {
    error_t err = err_SUCCESS;
    size_t num_fields = 14;
    size_t max_field_name_len = 32;
    size_t len_overhead = 2;
    size_t workspace_size = num_shards * (len_overhead + sizeof(ohtable_statistics) + num_fields*(max_field_name_len+8)) + 128;
    uint8_t *workspace;
    CHECK(workspace = calloc(workspace_size, 1));

    struct org_signal_cdsi_table_statistics_t *pb_stats = org_signal_cdsi_table_statistics_new(workspace, workspace_size);
    if (pb_stats == NULL) {
        err = err_HOST__TABLE_STATISTICS__PB_NEW;
        goto finish;
    } 

    int size = org_signal_cdsi_table_statistics_decode(pb_stats, pb, pbsize);
    if(size < 0) {
        TEST_LOG("failed to decode stats pb. pbsize: %zu returned: %d workspace_size: %zu", pbsize, size, workspace_size);
        err = err_HOST__TABLE_STATISTICS__PB_DECODE;
        goto finish;
    }

    for(size_t s = 0; s < num_shards; ++s) {
        struct org_signal_cdsi_shard_statistics_t shard_stats = pb_stats->shard_statistics.items_p[s];
        fprintf(stderr, "SHARD %zu: ", s);
        for(int i = 0; i < shard_stats.values.length; ++i) {
            fprintf(stderr, "%s: %" PRIu64 " ", shard_stats.values.items_p[i].name_p, shard_stats.values.items_p[i].value);

        }
        fprintf(stderr, "\n");
    }
    for(size_t i = 0; i < num_shards; ++i) {
        copy_pb_shard_stats_to_struct(pb_stats->shard_statistics.items_p[i], stats + i);
    }

finish:
    free(workspace);
    return err;
}
