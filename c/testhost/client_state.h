// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef CDS_TEST_HOST_CLIENT_STATE_H
#define CDS_TEST_HOST_CLIENT_STATE_H 1

#include <openenclave/host.h>
#include <noise/protocol/handshakestate.h>
#include <noise/protocol/cipherstate.h>
#include <noise/protocol.h>

#include "ratelimit/ratelimit.h"
#include "noiseutil/noise.h"

typedef struct
{
    oe_enclave_t *enclave;
    NoiseHandshakeState *handshake;
    NoiseCipherState *send;
    NoiseCipherState *recv;
    uint64_t client_id;
    bool has_token;
    uint8_t token[RATELIMIT_TOKEN_SIZE];

} enclave_client_state;

error_t extract_public_key(size_t ereport_size, uint8_t* ereport, uint8_t pubkey[NOISE_KEY_SIZE]);

int perform_handshake(enclave_client_state *ecs, bool simulate, uint8_t pubkey[NOISE_KEY_SIZE]);

#endif // CDS_TEST_HOST_CLIENT_STATE_H
