// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "proto/cdsi.h"
#include "proto/pbutil.h"
#include "util/util.h"
#include "util/tests.h"
#include <stdlib.h>

static uint8_t *create_query_pb(size_t num_e164s, uint64_t* e164s, size_t num_pairs, uint64_t* aci_uak_pairs, int *result_size)
{
    size_t workspace_size = num_pairs * 32 + num_e164s * 8 + 128;
    uint8_t *workspace = malloc(workspace_size);

    struct org_signal_cdsi_client_request_t *req = org_signal_cdsi_client_request_new(workspace, workspace_size);
    if (req == 0)
    {
        free(workspace);
        return 0;
    }
    req->aci_uak_pairs.buf_p = (uint8_t *)aci_uak_pairs;
    req->aci_uak_pairs.size = num_pairs * 4 * sizeof(*aci_uak_pairs);
    req->new_e164s.buf_p = (uint8_t *)e164s;
    req->new_e164s.size = num_e164s * sizeof(uint64_t);
    
    uint8_t *result = malloc(workspace_size);
    *result_size = org_signal_cdsi_client_request_encode(req, result, workspace_size);

    if (*result_size < 0)
    {
        free(result);
        result = 0;
    }

    free(workspace);
    return result;
}

#define NUM_E164S 1000
#define NUM_PAIRS 100

static error_t test_pb_workspace_for_decode() {
    uint64_t e164s[NUM_E164S] = {0};
    uint64_t pairs[4*NUM_PAIRS] = {0};
    int in_size;

    uint8_t* pb = create_query_pb(NUM_E164S, e164s, NUM_PAIRS, pairs, &in_size);
    size_t workspace_size = in_size + PBUTIL_WORKSPACE_BASE(struct org_signal_cdsi_client_request_t);
    uint8_t* workspace;
    RETURN_IF_ERROR(MALLOCZ_SIZE(workspace, workspace_size));
    struct org_signal_cdsi_client_request_t *req = org_signal_cdsi_client_request_new(workspace, workspace_size);
    TEST_ASSERT(req != NULL);

    int size = org_signal_cdsi_client_request_decode(req, pb, in_size);
    TEST_ASSERT(size > 0);
    TEST_ASSERT(req->aci_uak_pairs.size == 4*NUM_PAIRS*sizeof(pairs[0]));
    TEST_ASSERT(req->new_e164s.size == NUM_E164S*sizeof(e164s[0]));
    free(pb);
    return err_SUCCESS;
}


static error_t test_pb_workspace_for_encode() {
    size_t bad_response_workspace_size = PBUTIL_WORKSPACE_BASE(struct org_signal_cdsi_client_response_t) - alignof(struct org_signal_cdsi_client_response_t) - 1;
    uint8_t *response_workspace = calloc(bad_response_workspace_size, 1);
    
    struct org_signal_cdsi_client_response_t *rsp = org_signal_cdsi_client_response_new(response_workspace, bad_response_workspace_size);

    TEST_ASSERT(rsp == NULL);
    free(response_workspace);

    size_t response_workspace_size = PBUTIL_WORKSPACE_BASE(struct org_signal_cdsi_client_response_t);
    response_workspace = calloc(response_workspace_size, 1);
    
    TEST_LOG("response_workspace_size: %zu", response_workspace_size);
    rsp = org_signal_cdsi_client_response_new(response_workspace, response_workspace_size);

    TEST_ASSERT(rsp != NULL);

    // now add some data and see if we can encode
    size_t num_triples = 500;
    size_t sizeof_triple = 5*sizeof(uint64_t);

    rsp->e164_pni_aci_triples.buf_p = calloc(num_triples, sizeof_triple);
    rsp->e164_pni_aci_triples.size = num_triples*sizeof_triple;
    
    //now encode the response
    size_t pb_overhead = 8;
    size_t output_size = num_triples * sizeof_triple + pb_overhead;
    uint8_t* output = calloc(output_size, 1);

    int size = org_signal_cdsi_client_response_encode(rsp, output, output_size);
    TEST_ASSERT(size > 0);
    free(output);
    free(response_workspace);

    return err_SUCCESS;
}

int main() {
    RUN_TEST(test_pb_workspace_for_encode());
    RUN_TEST(test_pb_workspace_for_decode());
    return 0;
}
