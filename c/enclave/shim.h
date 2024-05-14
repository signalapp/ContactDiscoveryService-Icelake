// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef __CDSI_ENCLAVE_SHIM_H
#define __CDSI_ENCLAVE_SHIM_H

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION

#include "trust/cds_t.h"

#else

// Build fake versions of OpenEnclave functions that allow us to run outside
// of the enclave.

#include <string.h>
#include <stdlib.h>
#include <openenclave/bits/result.h>
#include <openenclave/bits/report.h>
#include <openenclave/log.h>

oe_result_t oe_random(void* buf, size_t len);
inline oe_result_t oe_random(void* buf, size_t len) {
  // Makes things deterministic, for fuzzing purposes.
  memset(buf, 0, len);
  return OE_OK;
}

oe_result_t oe_get_report(
    uint32_t flags,
    const uint8_t* report_data,
    size_t report_data_size,
    const void* opt_params,
    size_t opt_params_size,
    uint8_t** report_buffer,
    size_t* report_buffer_size);
inline oe_result_t oe_get_report(
    uint32_t flags,
    const uint8_t* report_data,
    size_t report_data_size,
    const void* opt_params,
    size_t opt_params_size,
    uint8_t** report_buffer,
    size_t* report_buffer_size) {
  *report_buffer = malloc(report_data_size);
  if (*report_buffer == NULL) return OE_OUT_OF_MEMORY;
  memcpy(*report_buffer, report_data, report_data_size);
  *report_buffer_size = report_data_size;
  return OE_OK;
}

void oe_free_report(uint8_t* report_buffer);
inline void oe_free_report(uint8_t* report_buffer) {
  free(report_buffer);
}

const char* oe_result_str(oe_result_t result);
inline const char* oe_result_str(oe_result_t result) {
  return "OE shimmed, no error type available";
}

oe_result_t oe_attester_initialize();
inline oe_result_t oe_attester_initialize() { return OE_OK; }

oe_result_t oe_attester_select_format(
    const oe_uuid_t* format_ids,
    size_t format_ids_length,
    oe_uuid_t* selected_format_id);
inline oe_result_t oe_attester_select_format(
    const oe_uuid_t* format_ids,
    size_t format_ids_length,
    oe_uuid_t* selected_format_id) {
  memcpy(selected_format_id, format_ids, sizeof(oe_uuid_t));
  return OE_OK;
}
oe_result_t oe_serialize_custom_claims(
    const oe_claim_t* custom_claims,
    size_t custom_claims_length,
    uint8_t** claims_out,
    size_t* claims_size_out);
inline oe_result_t oe_serialize_custom_claims(
    const oe_claim_t* custom_claims,
    size_t custom_claims_length,
    uint8_t** claims_out,
    size_t* claims_size_out) {
  *claims_out = calloc(1, 1);
  *claims_size_out = 1;
  return OE_OK;
}
oe_result_t oe_get_evidence(
    const oe_uuid_t* format_id,
    uint32_t flags,
    const void* custom_claims_buffer,
    size_t custom_claims_buffer_size,
    const void* optional_parameters,
    size_t optional_parameters_size,
    uint8_t** evidence_buffer,
    size_t* evidence_buffer_size,
    uint8_t** endorsements_buffer,
    size_t* endorsements_buffer_size) {
  *evidence_buffer = calloc(1, 1);
  *evidence_buffer_size = 1;
  *endorsements_buffer = calloc(1, 1);
  *endorsements_buffer_size = 1;
  return OE_OK;
}
oe_result_t oe_free_serialized_custom_claims(uint8_t* custom_claims_buffer);
inline oe_result_t oe_free_serialized_custom_claims(uint8_t* custom_claims_buffer) {
  free(custom_claims_buffer);
  return OE_OK;
}
oe_result_t oe_free_endorsements(uint8_t* endorsements_buffer);
inline oe_result_t oe_free_endorsements(uint8_t* endorsements_buffer) {
  free(endorsements_buffer);
  return OE_OK;
}
oe_result_t oe_free_evidence(uint8_t* evidence_buffer);
inline oe_result_t oe_free_evidence(uint8_t* evidence_buffer) {
  free(evidence_buffer);
  return OE_OK;
}

typedef void (*oe_enclave_log_callback_t)(
    void* context,
    oe_log_level_t level,
    uint64_t thread_id,
    const char* message);

oe_result_t oe_enclave_log_set_callback(void* context, oe_enclave_log_callback_t callback);
inline oe_result_t oe_enclave_log_set_callback(void* context, oe_enclave_log_callback_t callback) {
  return OE_OK;
}

oe_result_t oe_log_ocall(
    uint32_t log_level,
    const char* message);
inline oe_result_t oe_log_ocall(
    uint32_t log_level,
    const char* message) { return OE_OK; }


bool oe_is_within_enclave(const void* p, size_t s);
inline bool oe_is_within_enclave(const void* p, size_t s) { return true; }

#endif

#endif  // __CDSI_ENCLAVE_SHIM_H
