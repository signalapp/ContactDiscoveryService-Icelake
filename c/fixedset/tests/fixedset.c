// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#include <sys/random.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <sys/random.h>

#include "util/util.h"
#include "util/tests.h"
#include "fixedset/fixedset.h"

#define AERR(x) do { \
  error_t _e = (x); \
  if (_e != err_SUCCESS) { \
    fprintf(stderr, "FAILURE: %s:%d\n", __FILE__, __LINE__); \
    exit(1); \
  } \
} while(0)

int test_fixedset() {
  fixedset_t* s;
  uint8_t key[8] = {0};
  AERR(fixedset_new(&s, sizeof(int), key));
  int v = 1;
  bool found = false;
  assert(!fixedset_get(s, &v));
  AERR(fixedset_upsert(s, &v, &found));
  assert(!found);
  AERR(fixedset_upsert(s, &v, &found));
  assert(found);
  assert(fixedset_get(s, &v));
  v = 2;
  assert(!fixedset_get(s, &v));
  fixedset_free(s);
  return 0;
}

int test_fixedset_large() {
  fixedset_t* s;
  uint8_t key[8] = {0};
  AERR(fixedset_new(&s, sizeof(int), key));
  const int size = 100000;
  for (int v = 0; v < size; v++) {
    bool found = true;
    AERR(fixedset_upsert(s, &v, &found));
    assert(!found);
  }
  for (int v = 0; v < size; v++) {
    assert(fixedset_get(s, &v));
  }
  for (int v = 0; v < size; v++) {
    bool found = false;
    AERR(fixedset_upsert(s, &v, &found));
    assert(found);
  }
  for (int i = 0; i < size; i++) {
    int v = size + i;
    assert(!fixedset_get(s, &v));
  }
  for (int v = 0; v < size; v++) {
    assert(fixedset_remove(s, &v));
    assert(!fixedset_remove(s, &v));
  }
  fixedset_free(s);
  return 0;
}

int main()
{
    RUN_TEST(test_fixedset());
    RUN_TEST(test_fixedset_large());
    return 0;
}
