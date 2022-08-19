// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef _CDSI_UTIL_LOG_H
#define _CDSI_UTIL_LOG_H

#include <openenclave/log.h>

#include <jni.h>
#include <stdio.h>

typedef enum {
  SLF4J_LEVEL_TRACE,
  SLF4J_LEVEL_DEBUG,
  SLF4J_LEVEL_INFO,
  SLF4J_LEVEL_WARN,
  SLF4J_LEVEL_ERROR
} slf4j_log_level_t;
void slf4j_log(slf4j_log_level_t level, const char* msg);

void log_init(JavaVM* vm);
void log_shutdown();

#ifdef IS_TEST
#include <sys/time.h>

static inline int64_t time_micros(void) {
  struct timeval tv;
  if (0 != gettimeofday(&tv, NULL)) return -1;
  return tv.tv_usec + (1000000 * tv.tv_sec);
}

// We write everything to a temporary buffer, then write the whole buffer at once to stderr,
// because that causes less interleaving of loglines in multithreaded application.
#define TEST_LOG(format, ...) do { \
  char _buf[256] = {0}; \
  snprintf(_buf, sizeof(_buf), "LOG@%ld %s:%03d [%s] " format "\n", time_micros(), __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  fprintf(stderr, "%s", _buf); \
} while (0)

#define LOG_DEBUG(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) TEST_LOG(format, ##__VA_ARGS__)
#else
#define TEST_LOG(...)
#define LOG_AT_LEVEL(level, format, ...) do { \
  char _buf[256] = {0}; \
  snprintf(_buf, sizeof(_buf), format, ##__VA_ARGS__); \
  slf4j_log(SLF4J_LEVEL_##level, _buf); \
} while (0)
#define LOG_DEBUG(format, ...) LOG_AT_LEVEL(DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) LOG_AT_LEVEL(INFO, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) LOG_AT_LEVEL(WARN, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG_AT_LEVEL(ERROR, format, ##__VA_ARGS__)
#endif

#endif  // _CDSI_UTIL_LOG_H
