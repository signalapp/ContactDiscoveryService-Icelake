// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include "noiseutil/noise.h"
#include <noise/protocol/handshakestate.h>
#include <noise/protocol/cipherstate.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define ASSERT(x) do { \
  bool _x = (x); \
  if (!_x) { \
    fprintf(stderr, "Assert @ %s:%d: %s", __FILE__, __LINE__, #x); \
    exit(1); \
  } \
} while (0)

#define NOISE_ASSERT(x) ASSERT(NOISE_ERROR_NONE == (x))

#define BUFSIZE 65536*3

static void test_noise_message_encrypt_decrypt() {
  NoiseHandshakeState* ah;
  NoiseHandshakeState* bh;
  NoiseProtocolId id;
  memcpy(&id, &cdsi_client_protocol_id, sizeof(id));
  id.pattern_id = NOISE_PATTERN_NN_HFS;
  NOISE_ASSERT(noise_handshakestate_new_by_id(
      &ah, &id, NOISE_ROLE_INITIATOR));
  NOISE_ASSERT(noise_handshakestate_new_by_id(
      &bh, &id, NOISE_ROLE_RESPONDER));
  NOISE_ASSERT(noise_handshakestate_start(ah));
  NOISE_ASSERT(noise_handshakestate_start(bh));

  NoiseCipherState* arx;
  NoiseCipherState* atx;
  NoiseCipherState* brx;
  NoiseCipherState* btx;
  uint8_t* bufbuf = calloc(1, BUFSIZE);
  NoiseBuffer buf;
  noise_buffer_set_output(buf, bufbuf, BUFSIZE);
  NOISE_ASSERT(noise_handshakestate_write_message(ah, &buf, NULL));
  noise_buffer_set_input(buf, bufbuf, buf.size);
  NOISE_ASSERT(noise_handshakestate_read_message(bh, &buf, NULL));
  noise_buffer_set_output(buf, bufbuf, BUFSIZE);
  NOISE_ASSERT(noise_handshakestate_write_message(bh, &buf, NULL));
  NOISE_ASSERT(noise_handshakestate_split(bh, &btx, &brx));
  noise_buffer_set_input(buf, bufbuf, buf.size);
  NOISE_ASSERT(noise_handshakestate_read_message(ah, &buf, NULL));
  NOISE_ASSERT(noise_handshakestate_split(ah, &atx, &arx));

  uint8_t* plaintext = calloc(1, BUFSIZE);
  size_t ciphertext_size = BUFSIZE*2;
  uint8_t* ciphertext = calloc(1, ciphertext_size);
  ASSERT(err_SUCCESS == noise_encrypt_message(atx, plaintext, BUFSIZE, ciphertext, &ciphertext_size));
  ASSERT(ciphertext_size == BUFSIZE + 16 * 4);
  ASSERT(err_SUCCESS == noise_decrypt_message_inplace(brx, ciphertext, &ciphertext_size));
  ASSERT(ciphertext_size == BUFSIZE);
  ASSERT(0 == memcmp(plaintext, ciphertext, BUFSIZE));

  noise_handshakestate_free(ah);
  noise_handshakestate_free(bh);
  noise_cipherstate_free(atx);
  noise_cipherstate_free(arx);
  noise_cipherstate_free(btx);
  noise_cipherstate_free(brx);
  free(bufbuf);
  free(plaintext);
  free(ciphertext);
}

int main(int argc, char** argv) {
  printf("Running\n");
  test_noise_message_encrypt_decrypt();
}
