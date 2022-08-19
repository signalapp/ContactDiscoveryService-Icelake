// Copyright 2022-2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __CDSI_ENC_LOG_H
#define __CDSI_ENC_LOG_H

#include "trust/cds_t.h"
#include "util/log.h"

#ifdef IS_TEST
#define ENC_LOG_DEBUG(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#define ENC_LOG_INFO(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#define ENC_LOG_WARN(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#define ENC_LOG_ERROR(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#else

#include <openenclave/log.h>

#define ENC_LOG(level, format, ...) do { \
  char _buf[256] = {0}; \
  snprintf(_buf, sizeof(_buf), format, ##__VA_ARGS__); \
  CHECK(OE_OK == oe_log_ocall(OE_LOG_LEVEL_##level, _buf)); \
} while (0)

#define ENC_LOG_DEBUG(format, ...) ENC_LOG(VERBOSE, format, ##__VA_ARGS__)
#define ENC_LOG_INFO(format, ...) ENC_LOG(INFO, format, ##__VA_ARGS__)
#define ENC_LOG_WARN(format, ...) ENC_LOG(WARNING, format, ##__VA_ARGS__)
#define ENC_LOG_ERROR(format, ...) ENC_LOG(ERROR, format, ##__VA_ARGS__)
#endif

#endif  // __CDSI_ENC_LOG_H
