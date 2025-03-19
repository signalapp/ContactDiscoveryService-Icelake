// Copyright 2022-2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _CDSI_NOISE_H
#define _CDSI_NOISE_H

#include <noise/protocol.h>
#define NOISE_MAX_OVERHEAD 64
#define NOISE_KEY_SIZE 32
#define NOISE_MAX_PACKET_SIZE NOISE_MAX_PAYLOAD_LEN
#define NOISE_MAC_SIZE 16
#define NOISE_MAX_DATA_SIZE (NOISE_MAX_PACKET_SIZE - NOISE_MAC_SIZE)
#define NOISE_HANDSHAKEWRITE_SIZE 1632

#include "util/util.h"

extern const NoiseProtocolId cdsi_client_protocol_id;

#define NOISE_ERROR_BASE NOISE_ID('E', 0)
error_t noise_errort(error_t space, int noise_error);

/** noise_encrypt_message encrypts a plaintext message into ciphertext.
 *
 * Unlike the normal noise_cipherstate_encrypt, this method handles
 * plaintext sizes greater than NOISE_MAX_PACKET_SIZE (64K), by making
 * the output ciphertext from an ordered concatenated set of Noise
 * packets.
 *
 * Args:
 *  @param tx The cipherstate to use for encryption.
 *  @param plaintext_data The data to encrypt
 *  @param plaintext_size The size of the data to encrypt
 *  @param ciphertext_data The location to put the ciphertext
 *  @param ciphertext_size The initial size of the ciphertext_data buffer,
 *      which will be overwritten by the final size of the ciphertext.
 */
error_t noise_encrypt_message(
    NoiseCipherState* tx,
    const unsigned char* plaintext_data,
    size_t plaintext_size,
    unsigned char* ciphertext_data,
    size_t* ciphertext_size);

/** noise_decrypt_message decrypts a plaintext message into ciphertext.
 *
 * Unlike the normal noise_cipherstate_decrypt, this method handles
 * ciphertext sizes greater than NOISE_MAX_PACKET_SIZE (64K), by treating
 * the input ciphertext from an ordered concatenated set of Noise
 * packets.
 *
 * Args:
 *  @param rx The cipherstate to use for decryption.
 *  @param ciphertext_data The data to decrypt
 *  @param ciphertext_size The size of the data to decrypt
 *  @param plaintext_data The location to put the plaintext
 *  @param plaintext_size The initial size of the plaintext_data buffer,
 *      which will be overwritten by the final size of the plaintext.
 */
error_t noise_decrypt_message(
    NoiseCipherState* rx,
    const unsigned char* ciphertext_data,
    size_t ciphertext_size,
    unsigned char* plaintext_data,
    size_t* plaintext_size);

/** noise_decrypt_message_inplace works like noise_decrypt_message,
 *  but it treats its input buffer as its output buffer, overwriting
 *  it rather than writing to a new buffer.
 *
 * Args:
 *  @param rx The cipherstate to use for decryption.
 *  @param ciphertext_plaintext_data The buffer to be decrypted and written back to
 *  @param ciphertext_plaintext_size The initial size of the ciphertext_plaintext_data
 *      buffer, which will be overwritten by the final size of the plaintext.
 */
error_t noise_decrypt_message_inplace(
    NoiseCipherState* rx,
    unsigned char* ciphertext_plaintext_data,
    size_t* ciphertext_plaintext_size);

/** Derives a public key from a private key.
 *
 * Args:
 *  @param privkey Private key to derive from
 *  @param pubkey Public key to write to
 */
error_t noise_pubkey_from_privkey(
    const unsigned char privkey[NOISE_KEY_SIZE],
    unsigned char pubkey[NOISE_KEY_SIZE]);

#endif  // _CDSI_NOISE_H
