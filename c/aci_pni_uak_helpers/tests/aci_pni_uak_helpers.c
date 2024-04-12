// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <sys/random.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <sys/random.h>

#include "aci_pni_uak_helpers/aci_pni_uak_helpers.h"
#include "util/util.h"
#include "util/tests.h"

signal_user_record *generate_user_records(size_t num_records)
{
    signal_user_record *data = calloc(num_records, sizeof(*data));
    for (size_t i = 0; i < num_records; ++i)
    {
        uint64_t buf[7];
        getentropy(buf, sizeof buf);
        data[i].e164 = buf[0];
        data[i].aci[0] = buf[1];
        data[i].aci[1] = buf[2];
        data[i].pni[0] = buf[3];
        data[i].pni[1] = buf[4];
        data[i].uak[0] = buf[5];
        data[i].uak[1] = buf[6];
    }
    return data;
}

signal_user_record *generate_mock_table_response(size_t num_records, signal_user_record sent_records[])
{
    signal_user_record *result = calloc(num_records, sizeof(*result));
    for (size_t i = 0; i < num_records; ++i)
    {
        if (rand() % 2 == 1)
        {
            memset(&(result[i]), 255, sizeof(result[i]));
        }
        else
        {
            memcpy(&(result[i]), &(sent_records[i]), sizeof(sent_records[i]));
        }
    }
    return result;
}

aci_uak_pair *pull_good_aci_uak_pairs(size_t num_to_pull, size_t num_records, signal_user_record records[])
{
    assert(num_to_pull <= num_records);
    aci_uak_pair *pairs = calloc(num_to_pull, sizeof(*pairs));

    for (size_t i = 0; i < num_to_pull; ++i)
    {
        pairs[i].aci[0] = records[i].aci[0];
        pairs[i].aci[1] = records[i].aci[1];
        pairs[i].uak[0] = records[i].uak[0];
        pairs[i].uak[1] = records[i].uak[1];
    }
    return pairs;
}

aci_uak_pair *pull_bad_aci_uak_pairs(size_t num_to_pull, size_t num_records, signal_user_record records[])
{
    assert(num_to_pull <= num_records);
    aci_uak_pair *pairs = calloc(num_to_pull, sizeof(*pairs));

    for (size_t i = 0; i < num_to_pull; ++i)
    {
        pairs[i].aci[0] = records[i].aci[0];
        pairs[i].aci[1] = records[i].aci[1];
        pairs[i].uak[0] = records[i].uak[0] + 1;
        pairs[i].uak[1] = records[i].uak[1] + 1;
    }
    return pairs;
}
uint64_t *pull_e164s(size_t num_records, signal_user_record records[])
{
    uint64_t *result = calloc(num_records, sizeof(*result));
    for (size_t i = 0; i < num_records; ++i)
    {
        result[i] = records[i].e164;
    }
    return result;
}

int test_output_triples_all_pairs_correct()
{
    const char *desc = "compute output triples when all aci-uak pairs are present";
    printf("test: %s\n", desc);
    int retval = 0;

    size_t num_records = 100;
    signal_user_record *records = generate_user_records(num_records);
    signal_user_record *table_response = generate_mock_table_response(num_records, records);
    signal_user_record *response_copy = malloc(num_records * sizeof(*response_copy));
    memcpy(response_copy, table_response, num_records * sizeof(*response_copy));
    aci_uak_pair *pairs = pull_good_aci_uak_pairs(num_records, num_records, records);
    uint64_t *e164s = pull_e164s(num_records, records);

    e164_pni_aci_triple *out_triples = calloc(num_records, sizeof(*out_triples));

    uint8_t hash_key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    fixedset_t* index;
    RETURN_IF_ERROR(fixedset_new(&index, sizeof(aci_uak_pair), hash_key));
    RETURN_IF_ERROR(load_aci_uak_pairs(index, num_records, pairs));

    create_e164_pni_aci_triples(index, num_records, e164s, response_copy, out_triples);

    // now check that for every record that was in the result, the full triple is present
    for (size_t i = 0; i < num_records; ++i)
    {
        if (table_response[i].e164 != UINT64_MAX)
        {
            // this was present so should be in the results with full triple
            bool found = false;
            for (size_t j = 0; j < num_records; ++j)
            {
                if (out_triples[j].e164 == table_response[i].e164)
                {
                    found = true;
                    // printf("%lu: %lu, %lu, %lu, %lu\n",
                    //        i,
                    //        out_triples[j].aci[0],
                    //        out_triples[j].aci[1],
                    //        out_triples[j].pni[0],
                    //        out_triples[j].pni[1]);
                    assert(out_triples[j].aci[0] == table_response[i].aci[0]);
                    assert(out_triples[j].aci[1] == table_response[i].aci[1]);
                    assert(out_triples[j].pni[0] == table_response[i].pni[0]);
                    assert(out_triples[j].pni[1] == table_response[i].pni[1]);
                }
            }
            assert(found);
        }
        else
        {
            // this was NOT present so should not be there
            bool found = false;
            for (size_t j = 0; j < num_records; ++j)
            {
                if (out_triples[j].e164 == records[i].e164)
                {
                    found = true;
                }
            }
            assert(!found);
        }
    }

    fixedset_free(index);
    free(out_triples);
    free(e164s);
    free(pairs);
    free(response_copy);
    free(table_response);
    free(records);
    return retval;
}

int test_output_triples_no_pairs_correct()
{
    const char *desc = "compute output triples when all aci-uak pairs are incorrect";
    printf("test: %s\n", desc);
    int retval = 0;

    size_t num_records = 100;
    signal_user_record *records = generate_user_records(num_records);
    signal_user_record *table_response = generate_mock_table_response(num_records, records);
    signal_user_record *response_copy = malloc(num_records * sizeof(*response_copy));
    memcpy(response_copy, table_response, num_records * sizeof(*response_copy));
    aci_uak_pair *pairs = pull_bad_aci_uak_pairs(num_records, num_records, records);
    uint64_t *e164s = pull_e164s(num_records, records);

    e164_pni_aci_triple *out_triples = calloc(num_records, sizeof(*out_triples));

    uint8_t hash_key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    fixedset_t* index;
    RETURN_IF_ERROR(fixedset_new(&index, sizeof(aci_uak_pair), hash_key));
    RETURN_IF_ERROR(load_aci_uak_pairs(index, num_records, pairs));

    create_e164_pni_aci_triples(index, num_records, e164s, response_copy, out_triples);

    // now check that for every record that was in the result, the full triple is present
    for (size_t i = 0; i < num_records; ++i)
    {
        if (table_response[i].e164 != UINT64_MAX)
        {
            // this was present so should be in the results with full triple
            bool found = false;
            for (size_t j = 0; j < num_records; ++j)
            {
                if (out_triples[j].e164 == table_response[i].e164)
                {
                    found = true;
                    // printf("%lu: %lu, %lu, %lu, %lu\n",
                    //        i,
                    //        out_triples[j].aci[0],
                    //        out_triples[j].aci[1],
                    //        out_triples[j].pni[0],
                    //        out_triples[j].pni[1]);
                    assert(out_triples[j].aci[0] == 0);
                    assert(out_triples[j].aci[1] == 0);
                    assert(out_triples[j].pni[0] == table_response[i].pni[0]);
                    assert(out_triples[j].pni[1] == table_response[i].pni[1]);
                }
            }
            assert(found);
        }
        else
        {
            // this was NOT present so should not be there
            bool found = false;
            for (size_t j = 0; j < num_records; ++j)
            {
                if (out_triples[j].e164 == records[i].e164)
                {
                    found = true;
                }
            }
            assert(!found);
        }
    }

    fixedset_free(index);
    free(out_triples);
    free(e164s);
    free(pairs);
    free(response_copy);
    free(table_response);
    free(records);
    return retval;
}

int main()
{
    RUN_TEST(test_output_triples_all_pairs_correct());
    RUN_TEST(test_output_triples_no_pairs_correct());
    return 0;
}
