// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdio.h>

#include <openenclave/attestation/verifier.h>
#include <openenclave/attestation/custom_claims.h>
#include <openenclave/attestation/sgx/evidence.h>

#include "untrust/cds_u.h"
#include "proto/cdsi.h"
#include "client_state.h"
#include "util/util.h"

static oe_uuid_t sgx_remote_uuid = {OE_FORMAT_UUID_SGX_ECDSA};

static const oe_claim_t* _find_claim(
    const oe_claim_t* claims,
    size_t claims_size,
    const char* name)
{
    for (size_t i = 0; i < claims_size; i++)
    {
        if (strcmp(claims[i].name, name) == 0)
            return &(claims[i]);
    }
    return NULL;
}

error_t extract_public_key(size_t pb_size, uint8_t *pb, uint8_t pubkey[NOISE_KEY_SIZE])
{
    error_t result = err_SUCCESS;
    oe_result_t oe_result = OE_OK;
    oe_claim_t* claims = NULL;
    size_t claims_length = 0;
    const oe_claim_t* claim;
    oe_claim_t* custom_claims = NULL;
    size_t custom_claims_length = 0;

    uint8_t workspace[(32 << 10)];

    struct org_signal_cdsi_client_handshake_start_t *rsp = org_signal_cdsi_client_handshake_start_new(workspace, sizeof(workspace));  
    ASSERT_ERR(rsp != 0, err_ENCLAVE__HANDSHAKE__EXTRACT_PUBKEY_PB);

    int size = org_signal_cdsi_client_handshake_start_decode(rsp, pb, pb_size);
    ASSERT_ERR(size >= 0, err_ENCLAVE__HANDSHAKE__EXTRACT_PUBKEY_PB);

    if(rsp->evidence.size > 0) {
        ASSERT_ERR(OE_OK == oe_verifier_initialize(), err_HOST__HANDSHAKE__INITIALIZE_VERIFIER);
        // verify the quote and collateral
        fprintf(stderr, "verify evidence\n");
        oe_result = oe_verify_evidence(
            &sgx_remote_uuid,
            rsp->evidence.buf_p,
            rsp->evidence.size,
            rsp->endorsement.buf_p,
            rsp->endorsement.size,
            NULL,
            0,
            &claims,
            &claims_length);
        if(oe_result != OE_OK) {
            fprintf(stderr, "error verifying evidence: %s\n", oe_result_str(oe_result));
            result = err_HOST__HANDSHAKE__INVALID_REPORT;
            goto exit;
        }

        // Now get the custom claims
        claim = _find_claim(claims, claims_length, OE_CLAIM_CUSTOM_CLAIMS_BUFFER);
        ASSERT_ERR(claim != NULL, err_HOST__HANDSHAKE__INVALID_CLAIMS);
        oe_result = oe_deserialize_custom_claims(
                        claim->value,
                        claim->value_size,
                        &custom_claims,
                        &custom_claims_length);
        if(oe_result != OE_OK) {
            fprintf(stderr, "error deserializing custom claims: %s\n", oe_result_str(oe_result));
            result = err_HOST__HANDSHAKE__INVALID_CLAIMS;
            goto exit;
        }
        // the public key is in the first custom claim
        if(custom_claims_length != 1) {
            fprintf(stderr, "wrong number of custom claims: %zu\n", custom_claims_length);
            result = err_HOST__HANDSHAKE__INVALID_CLAIMS;
            goto exit;
        }

        if(strcmp("pk", custom_claims[0].name) != 0) {
            fprintf(stderr, "wrong custom claim name: %s\n", custom_claims[0].name);
            result = err_HOST__HANDSHAKE__INVALID_CLAIMS;
            goto exit;  
        }

        if(memcmp(rsp->test_only_pubkey.buf_p, custom_claims[0].value, custom_claims[0].value_size) != 0) {
            fprintf(stderr, "wrong custom claim value: %u - %u\n", rsp->test_only_pubkey.buf_p[0], custom_claims[0].value[0]);
            result = err_HOST__HANDSHAKE__INVALID_CLAIMS;
            goto exit;  
        }
        fprintf(stderr, "verified quote, collateral, and custom claim\n");

    }
    memcpy(pubkey, rsp->test_only_pubkey.buf_p, NOISE_KEY_SIZE);

exit:
    if(claims) oe_free_claims(claims, claims_length);
    if(custom_claims) oe_free_custom_claims(custom_claims, custom_claims_length);
    return result;
}

int perform_handshake(enclave_client_state *ecs, bool simulate, uint8_t pubkey[NOISE_KEY_SIZE])
{
    int err, retval, result;
    RETURN_IF_ERROR(noise_errort(err_NOISE__HANDSHAKESTATE__NEW__, noise_handshakestate_new_by_id(&ecs->handshake, &cdsi_client_protocol_id, NOISE_ROLE_INITIATOR)));
    NoiseDHState *dhstate = noise_handshakestate_get_remote_public_key_dh(ecs->handshake);
    RETURN_IF_ERROR(noise_errort(err_NOISE__DHSTATE__SET_PUBLIC_KEY__, noise_dhstate_set_public_key(dhstate, pubkey, NOISE_KEY_SIZE)));
    RETURN_IF_ERROR(noise_errort(err_NOISE__HANDSHAKESTATE__START__, noise_handshakestate_start(ecs->handshake)));

    if (noise_handshakestate_get_action(ecs->handshake) != NOISE_ACTION_WRITE_MESSAGE)
    {
        return err_HOST__HANDSHAKE__INVALID_STATE;
    }
    NoiseBuffer mbuf;
    uint8_t message[4096];
    noise_buffer_set_output(mbuf, message, sizeof(message));
    RETURN_IF_ERROR(noise_errort(err_NOISE__HANDSHAKESTATE__WRITE__, noise_handshakestate_write_message(ecs->handshake, &mbuf, NULL)));

    size_t in_size = mbuf.size;
    CHECK(in_size == NOISE_HANDSHAKEWRITE_SIZE);
    uint8_t *in = message;
    uint8_t out[8192];
    size_t actual_out_size = 0;
    result = enclave_handshake(ecs->enclave, &retval, ecs->client_id, in_size, in, sizeof(out), out, &actual_out_size);
    if (result != OE_OK)
    {
        fprintf(
            stderr,
            "calling into enclave_handshake failed: result=%u (%s)\n",
            result,
            oe_result_str(result));
        return err_HOST__HANDSHAKE__CALL_ENCLAVE;
    }
    if (retval != 0)
    {
        fprintf(
            stderr,
            "enclave_handshake failed internally: result=%u\n",
            retval);
        return retval;
    }

    if (noise_handshakestate_get_action(ecs->handshake) != NOISE_ACTION_READ_MESSAGE)
    {
        return err_HOST__HANDSHAKE__INVALID_STATE;
    }
    noise_buffer_set_input(mbuf, out, actual_out_size);
    err = noise_handshakestate_read_message(ecs->handshake, &mbuf, NULL);
    if (err != NOISE_ERROR_NONE)
    {
        noise_perror("read handshake", err);
        return err_HOST__HANDSHAKE__READ;
    }
    if (noise_handshakestate_get_action(ecs->handshake) != NOISE_ACTION_SPLIT)
    {
        return err_HOST__HANDSHAKE__INVALID_STATE;
    }

    if (NOISE_ERROR_NONE != noise_handshakestate_split(ecs->handshake, &ecs->send, &ecs->recv))
    {
        return err_HOST__HANDSHAKE__SPLIT;
    }
    if (noise_handshakestate_get_action(ecs->handshake) != NOISE_ACTION_COMPLETE)
    {
        return err_HOST__HANDSHAKE__INVALID_STATE;
    }

    return err_SUCCESS;
}
