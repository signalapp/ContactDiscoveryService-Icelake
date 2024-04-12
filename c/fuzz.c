// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <noise/protocol.h>

#include "trust/cds_t.h"
#include "util/util.h"
#include "noiseutil/noise.h"

#define ASSERT(x) do { \
  bool _x = (x); \
  if (!_x) { \
    fprintf(stderr, "Assert @ %s:%d: %s", __FILE__, __LINE__, #x); \
    exit(1); \
  } \
} while (0)

#define NOISE_ASSERT(x) ASSERT(NOISE_ERROR_NONE == (x))

void* run_shard(void* unused) {
  enclave_run_shard(0);
  return NULL;
}

typedef enum {
  qft_none,
  qft_handshake_request,
  qft_ratelimit_request_plaintext,
  qft_ratelimit_request_ciphertext,
  qft_run_request_plaintext,
  qft_run_request_ciphertext,
} queryfuzz_target_t;

void query(size_t in_size, uint8_t* in, queryfuzz_target_t target) {
  uint64_t cli;
  NoiseHandshakeState* hs;
  {
    uint8_t key[32];
    size_t key_size;
    ASSERT(0 == enclave_attest());
    ASSERT(0 == enclave_new_client(&cli, sizeof(key), key, &key_size));
    NOISE_ASSERT(noise_handshakestate_new_by_name(
        &hs, "Noise_NK_25519_ChaChaPoly_SHA256", NOISE_ROLE_INITIATOR));
    NoiseDHState* ds = noise_handshakestate_get_remote_public_key_dh(hs);
    NOISE_ASSERT(noise_dhstate_set_public_key(ds, key, key_size));
  }
  NOISE_ASSERT(noise_handshakestate_start(hs));
  NoiseBuffer b;
  NoiseCipherState* tx;
  NoiseCipherState* rx;

  uint8_t* buf;
  size_t bufsize;
  {
    uint8_t hsreq[1024];
    if (target == qft_handshake_request) {
      buf = in; bufsize = in_size;
    } else {
      noise_buffer_set_output(b, hsreq, sizeof(hsreq));
      NOISE_ASSERT(noise_handshakestate_write_message(hs, &b, NULL));
      buf = hsreq; bufsize = b.size;
    }
    uint8_t hsresp[1024];
    size_t hsresp_size;
    ASSERT(0 == enclave_handshake(cli, bufsize, buf, sizeof(hsresp), hsresp, &hsresp_size));
    noise_buffer_set_input(b, hsresp, hsresp_size);
    NOISE_ASSERT(noise_handshakestate_read_message(hs, &b, NULL));
    NOISE_ASSERT(noise_handshakestate_split(hs, &tx, &rx));
  }
  noise_handshakestate_free(hs);
  {
    uint8_t req_in[] = {
      (0x03 << 3) | 0x02,  // field 3=new_e164s, type 2=bytes
      0x08,  // length=8
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // value
      (0x01 << 3) | 0x02,  // field 1=aci_uak_pairs, type 2=bytes
      0x20,  // length=32
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    };
    uint8_t req[2048];
    size_t req_size = sizeof(req);
    if (target == qft_ratelimit_request_plaintext) {
      buf = in; bufsize = in_size;
    } else {
      buf = req_in; bufsize = sizeof(req_in);
    }
    ASSERT(err_SUCCESS == noise_encrypt_message(tx, buf, bufsize, req, &req_size));
    uint8_t out[128];
    size_t out_size;
    uint8_t old_hash[32];
    size_t old_hash_size;
    uint8_t new_hash[32];
    size_t new_hash_size;
    size_t computed_size;
    if (target == qft_ratelimit_request_ciphertext) {
      buf = in; bufsize = in_size;
    } else {
      buf = req; bufsize = req_size;
    }
    ASSERT(0 == enclave_rate_limit(
        cli,
        bufsize, buf,
        sizeof(out), out, &out_size,
        sizeof(old_hash), old_hash, &old_hash_size,
        sizeof(new_hash), new_hash, &new_hash_size,
        &computed_size));
    ASSERT(err_SUCCESS == noise_decrypt_message_inplace(rx, out, &out_size));
  }
  {
    uint8_t req_in[] = {
      (0x07 << 3) | 0x00,  // field 7=token_ack, type 0=varint
      0x01,  // value=1
    };
    uint8_t req[2048];
    size_t req_size = sizeof(req);
    if (target == qft_run_request_plaintext) {
      buf = in; bufsize = in_size;
    } else {
      buf = req_in; bufsize = sizeof(req_in);
    }
    ASSERT(err_SUCCESS == noise_encrypt_message(tx, buf, bufsize, req, &req_size));
    uint8_t out[128];
    size_t out_size;
    if (target == qft_run_request_ciphertext) {
      buf = in; bufsize = in_size;
    } else {
      buf = req; bufsize = req_size;
    }
    ASSERT(0 == enclave_run(
        cli,
        0,
        bufsize, buf,
        sizeof(out), out, &out_size));
    ASSERT(err_SUCCESS == noise_decrypt_message_inplace(rx, out, &out_size));
  }
  ASSERT(0 == enclave_close_client(cli));
  noise_cipherstate_free(tx);
  noise_cipherstate_free(rx);
}

int main(int argc, char** argv) {
  unsigned char in[2<<20];
  size_t in_size = 0;
  while (in_size < sizeof(in)) {
    ssize_t got = read(0 /*stdin*/, in+in_size, sizeof(in)-in_size);
    if (got == 0) {
      break;
    } else if (got < 0) {
      perror("reading");
      exit(1);
    } else {
      in_size += got;
    }
  }
  ASSERT(in_size > 0);
  ASSERT(0 == enclave_init((1 << 20), 1.6, 1, 30));
  pthread_t tid;
  ASSERT(0 == pthread_create(&tid, NULL, run_shard, NULL));
  switch (in[0]) {
    case 0:
      // enclave_add_data(in_size-1, in+1);
      break;
    case 1:
      enclave_load_pb(in_size-1, in+1);
      break;
    case 2:
      query(in_size-1, in+1, qft_handshake_request);
      break;
    case 3:
      query(in_size-1, in+1, qft_ratelimit_request_plaintext);
      break;
    case 4:
      query(in_size-1, in+1, qft_ratelimit_request_ciphertext);
      break;
    case 5:
      query(in_size-1, in+1, qft_run_request_plaintext);
      break;
    case 6:
      query(in_size-1, in+1, qft_run_request_ciphertext);
      break;
    case 7:
      query(in_size-1, in+1, qft_none);
      break;
    default:
      return 1;
  }
  ASSERT(0 == enclave_stop_shards());
  pthread_join(tid, 0);
  return 0;
}
