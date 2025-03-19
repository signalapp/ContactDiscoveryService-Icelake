// Copyright 2022-2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only
#include "noise.h"
#include <noise/protocol/constants.h>
#include <string.h>

const NoiseProtocolId cdsi_client_protocol_id = {
    .prefix_id = NOISE_PREFIX_STANDARD,
#ifdef NO_NOISE_HFS
    .pattern_id = NOISE_PATTERN_NK,
#else
    .pattern_id = NOISE_PATTERN_NK_HFS,
#endif
    .dh_id = NOISE_DH_CURVE25519,
    .cipher_id = NOISE_CIPHER_CHACHAPOLY,
    .hash_id = NOISE_HASH_SHA256,
#ifdef NO_NOISE_HFS
    .hybrid_id = 0,
#else
    .hybrid_id = NOISE_DH_KYBER1024,
#endif
};

error_t noise_encrypt_message(
    NoiseCipherState* tx,
    const unsigned char* plaintext_data,
    size_t plaintext_size,
    unsigned char* ciphertext_data,
    size_t* ciphertext_size) {
  size_t plaintext_offset;
  size_t ciphertext_offset = 0;
  for (plaintext_offset = 0; plaintext_offset < plaintext_size; plaintext_offset += NOISE_MAX_DATA_SIZE) {
    size_t to_encrypt = NOISE_MAX_DATA_SIZE;
    if (plaintext_offset + to_encrypt > plaintext_size) {
      to_encrypt = plaintext_size - plaintext_offset;
    }
    memcpy(ciphertext_data+ciphertext_offset, plaintext_data+plaintext_offset, to_encrypt);
    NoiseBuffer buf;
    noise_buffer_set_inout(
        buf,
        ciphertext_data+ciphertext_offset,
        to_encrypt,
        *ciphertext_size-ciphertext_offset);
    RETURN_IF_ERROR(noise_errort(err_NOISE__CIPHERSTATE__ENCRYPT__, noise_cipherstate_encrypt(tx, &buf)));
    ciphertext_offset += buf.size;
  }
  *ciphertext_size = ciphertext_offset;
  return err_SUCCESS;
}

error_t noise_decrypt_message_inplace(
    NoiseCipherState* rx,
    unsigned char* ciphertext_plaintext_data,
    size_t* ciphertext_plaintext_size) {
  return noise_decrypt_message(
      rx,
      ciphertext_plaintext_data, *ciphertext_plaintext_size,
      ciphertext_plaintext_data, ciphertext_plaintext_size);
}

error_t noise_decrypt_message(
    NoiseCipherState* rx,
    const unsigned char* ciphertext_data,
    size_t ciphertext_size,
    unsigned char* plaintext_data,
    size_t* plaintext_size) {
  size_t plaintext_offset = 0;
  size_t ciphertext_offset;
  for (ciphertext_offset = 0; ciphertext_offset < ciphertext_size; ciphertext_offset += NOISE_MAX_PACKET_SIZE) {
    size_t to_decrypt = NOISE_MAX_PACKET_SIZE;
    if (ciphertext_offset + to_decrypt > ciphertext_size) {
      to_decrypt = ciphertext_size - ciphertext_offset;
    }
    // We must memmove over memcpy here because plaintext_data and ciphertext_data may
    // overlap in the case that this is called from noise_decrypt_message_inplace.
    memmove(plaintext_data+plaintext_offset, ciphertext_data+ciphertext_offset, to_decrypt);
    NoiseBuffer buf;
    noise_buffer_set_inout(buf, plaintext_data+plaintext_offset, to_decrypt, *plaintext_size-plaintext_offset);
    RETURN_IF_ERROR(noise_errort(err_NOISE__CIPHERSTATE__DECRYPT__, noise_cipherstate_decrypt(rx, &buf)));
    plaintext_offset += buf.size;
  }
  *plaintext_size = plaintext_offset;
  return err_SUCCESS;
}

error_t noise_errort(error_t space, int noise_error) {
  if (noise_error == NOISE_ERROR_NONE) return err_SUCCESS;
  return space + noise_error - NOISE_ERROR_BASE;
}

error_t noise_pubkey_from_privkey(
    const unsigned char privkey[NOISE_KEY_SIZE],
    unsigned char pubkey[NOISE_KEY_SIZE]) {
  NoiseDHState* state;
  error_t err = err_SUCCESS;
  RETURN_IF_ERROR(noise_errort(err_NOISE__DHSTATE__NEW__, noise_dhstate_new_by_id(&state, cdsi_client_protocol_id.dh_id)));
  GOTO_IF_ERROR(err = noise_errort(err_NOISE__DHSTATE__SET_KEYPAIR_PRIVATE__, noise_dhstate_set_keypair_private(state, privkey, NOISE_KEY_SIZE)), free_state);
  GOTO_IF_ERROR(err = noise_errort(err_NOISE__DHSTATE__GET_PUBLIC_KEY__, noise_dhstate_get_public_key(state, pubkey, NOISE_KEY_SIZE)), free_state);

free_state:
  noise_dhstate_free(state);
  return err;
}
