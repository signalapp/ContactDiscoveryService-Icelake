// Copyright 2022-2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only
#include <math.h>
#include <halfsiphash.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "fixedset.h"
#include "util/util.h"

struct fixedset_t {
  // This block contains all entries in a single contiguous memory space.
  // You can think of it as containing a repeated set of [jump][key][value]
  // entries concatenated together.
  unsigned char* block;
  // This block is the size of [ksize], and is utilized during
  // upserts where we need temporary storage to copy key/value pairs around
  // while keeping our RobinHood jumps correctly ordered/handled.
  unsigned char* tmpk;

  size_t ksize;    // Exact size of each key, in bytes
  size_t cap;      // Number of entries in [block]
  size_t size;     // Number of set entries in [block]

  // [maxjump] is a small optimization that stores the maximum [jump] value
  // across all entries in the current map.  When calling _get on a key,
  // we only have to look up to this many entries into the map before we
  // know that the key doesn't exist.
  size_t maxjump;

  // Per-table random key.
  unsigned char halfsiphash_key[FIXEDMAP_HALFSIPHASH_KEYSIZE];
};

typedef struct {
  // This entry is stored at index [hash(key)+jump] in the map.
  // [jump] will be non-zero if the entry contains a key/value pair,
  // or zero if the entry is empty.
  uint32_t jump;
  // Pointer to the start of the key/value pair stored in this entry.
  // Key stored at [kv], Value stored at [kv+h->ksize].
  unsigned char kv[0];
} fixedset_entry_t;

// Hash a key within the given map.
static inline size_t khash(fixedset_t* h, const unsigned char* k) {
  size_t out = 0;
  halfsiphash(k, h->ksize, h->halfsiphash_key, (unsigned char*) &out, sizeof(out));
  return out;
}

// Return whether two keys [a] and [b] are equal.
static bool keq(fixedset_t* h, const unsigned char* a, const unsigned char* b) {
  unsigned char out = 0;
  for (size_t i = 0; i < h->ksize; i++) {
    out |= a[i] ^ b[i];
  }
  return out == 0;
}

// Swap the data contained within two memory regions [a] and [b],
// both of size [s].  It is allowed that [a] == [b]
static void memswap(unsigned char* a, unsigned char* b, size_t s) {
  unsigned char buf[128];
  while (s > 0) {
    size_t to_copy = sizeof(buf) > s ? s : sizeof(buf);
    memcpy(buf, a, to_copy);
    memcpy(a, b, to_copy);
    memcpy(b, buf, to_copy);
    s -= to_copy;
    a += sizeof(buf);
    b += sizeof(buf);
  }
}

// Compute the total size in bytes of a fixedset_entry_t, including
// its associated key and value.
static size_t fixedset_entry_size(fixedset_t* h) {
  return h->ksize + sizeof(fixedset_entry_t);
}

// Return a pointer to an entry within the fixedset.  Note that [i]
// may be outside the range of [0,h->cap); this function will perform
// the correct modulo arithmetic to get a valid pointer.
static fixedset_entry_t* fixedset_entry_n(fixedset_t* h, size_t i) {
  return (fixedset_entry_t*) (h->block + (i % h->cap) * fixedset_entry_size(h));
}

error_t fixedset_new(fixedset_t** out, size_t ksize, unsigned char sipkey[FIXEDMAP_HALFSIPHASH_KEYSIZE]) {
  fixedset_t* h;
  MALLOCZ_OR_RETURN_ERROR(h);
  h->ksize = ksize;
  h->size = 0;
  h->maxjump = 0;
  h->cap = 32;
  memcpy(h->halfsiphash_key, sipkey, FIXEDMAP_HALFSIPHASH_KEYSIZE);
  error_t err = err_SUCCESS;
  GOTO_IF_ERROR(err = MALLOCZ_SIZE(h->block, h->cap*fixedset_entry_size(h)), oom);

  // When upserting, we sometimes need to swap around key/value pairs, and
  // we'd like to not have to malloc space for that within the upsert call.
  // So, we create a single key/value-sized buffer within the fixedset_t that
  // we utilize for this purpose.
  GOTO_IF_ERROR(err = MALLOCZ_SIZE(h->tmpk, h->ksize), oom);
  *out = h;
  return err_SUCCESS;

oom:
  fixedset_free(h);
  return err;
}

void fixedset_free(fixedset_t* h) {
  if (h->block) free(h->block);
  if (h->tmpk) free(h->tmpk);
  free(h);
}

// Increase the size of a fixedset to [newcap].
// We generate a brand new memory space, then copy all entries
// from the old space into the new one.  This is an expensive operation
// at O(n).
static error_t fixedset_resize(fixedset_t* h, size_t newcap) {
  ASSERT_ERR(newcap > h->cap, err_FIXEDSET__RESIZE_INVALID);
  fixedset_t old = *h;
  error_t err = err_OOM;
  GOTO_IF_ERROR(err = MALLOCZ_SIZE(h->block, newcap * fixedset_entry_size(h)), rollback);
  h->cap = newcap;
  h->size = 0;
  h->maxjump = 0;
  for (size_t i = 0; i < old.cap; i++) {
    fixedset_entry_t* e = fixedset_entry_n(&old, i);
    if (e->jump) {
      GOTO_IF_ERROR(err = fixedset_upsert(h, e->kv, NULL), freenew);
    }
  }
  free(old.block);
  return err_SUCCESS;

 freenew:
  free(h->block);
 rollback:
  *h = old;
  return err;
}

// Update [h->maxjump] with [jump].
static void fixedset_maybe_set_max_jump(fixedset_t* h, size_t jump) {
  if (jump > h->maxjump) {
    h->maxjump = jump;
  }
}

// Find a given key within the map, returning both the index at which
// it was found (in [*idx]) and a pointer to the entry that houses
// it (in [*out]).  If not found, sets [*out] to NULL and returns false.
static bool fixedset_find(fixedset_t* h, const unsigned char* k, size_t* idx, fixedset_entry_t** out) {
  size_t hash = khash(h, k);
  size_t jump;
  *idx = 0;
  uintptr_t outp = 0;
  for (jump = 1; jump <= h->maxjump; jump++) {
    fixedset_entry_t* e = fixedset_entry_n(h, hash + jump);
    // bitwise & operator because && short-circuits.
    bool got = keq(h, k, e->kv) & (e->jump == jump);
    *idx ^= (hash + jump) & (got ? ((size_t) 0)-1 : 0);
    outp ^= ((uintptr_t) e) & (got ? ((uintptr_t) 0)-1 : 0);
  }
  *out = (fixedset_entry_t*) outp;
  return outp != 0;
}

error_t fixedset_upsert(fixedset_t* h, const void* k_void, bool* replaced) {
  const unsigned char* k = (const unsigned char*) k_void;
  // We resize when we are 8/9 full (~88%).  Because we use RobinHood
  // hashing, the _get calls for this high a level of saturation is
  // still quite good.
  size_t captest = h->size + h->size/8;
  if (captest > h->cap) {
    // See the Lua use of NUM_BLOCKS for why the following makes sense:
    //
    // When we grow, we grow randomly by somewhere between 25% and 75%.
    // This randomness makes a big difference when keys are uniformly
    // distributed between blocks, as it avoids having all blocks grow
    // at once.  If, for example, we grew by 1/2 all the time, all blocks
    // would grow at about the same time, dropping our memory utilization
    // from 8/9 -> 16/27 (88% -> 59%).  By making blocks be different
    // sizes, we tend to allow a more consistent memory utilization.
    size_t new_cap = h->cap + h->cap / 4 + rand() % (h->cap / 2);

    // This set of resizing computations (deciding when to resize and what
    // to resize to) in practice provides roughly a 70% utilization of
    // the hash.
    RETURN_IF_ERROR(fixedset_resize(h, new_cap));
  }
  fixedset_entry_t* e;
  size_t idx = 0;
  if (fixedset_find(h, k, &idx, &e)) {
    if (replaced) *replaced = true;
    return err_SUCCESS;
  }

  // This loop is the core of our RobinHood hashing, modeled on
  // https://programming.guide/robin-hood-hashing.html.  As we insert, we
  // find a place for our own key, potentially dislodging other keys
  // in the process.  Every time we dislodge a key, we then continue
  // to find a place for it.  We end when we insert a key/value pair into
  // an _empty_ part of the hashmap.
  size_t hash = khash(h, k);
  size_t jump;
  for (jump = 1; ; jump++) {
    fixedset_entry_t* e = fixedset_entry_n(h, hash + jump);
    if (e->jump == 0) {
      // We've found an empty slot.  Insert the current key/value,
      // and we're done.
      e->jump = jump;
      memcpy(e->kv, k, h->ksize);
      if (replaced) *replaced = false;
      h->size++;
      fixedset_maybe_set_max_jump(h, jump);
      return err_SUCCESS;
    } else if (e->jump < jump) {
      // We've found an entry with a lower jump than us.
      // Do the RobinHood thing:
      //   - take that spot
      //   - then look for a new spot for the new key/value
      hash = hash + jump - e->jump;
      size_t tmpjump = e->jump;
      e->jump = jump;
      fixedset_maybe_set_max_jump(h, jump);
      jump = tmpjump;
      // The passed-in key/value are const pointers, so we can't copy
      // the data from the key/value we found in the map into them.
      // Rather than doing that, copy the passed-in key/value into
      // our tmpk storage, then utilize that from now on for memswaps.
      if (k != h->tmpk) {
        memcpy(h->tmpk, k, h->ksize);
        k = h->tmpk;
      }
      memswap(e->kv, h->tmpk, h->ksize);
    }
  }
}

bool fixedset_get(fixedset_t* h, const void* k_void) {
  const unsigned char* k = (const unsigned char*) k_void;
  fixedset_entry_t* e;
  size_t idx = 0;
  return fixedset_find(h, k, &idx, &e);
}

bool fixedset_remove(fixedset_t* h, const void* k_void) {
  const unsigned char* k = (const unsigned char*) k_void;
  fixedset_entry_t* e;
  uintptr_t idx;
  if (!fixedset_find(h, k, &idx, &e)) return false;

  h->size--;
  size_t i;
  for (i = 1; ; i++) {
    fixedset_entry_t* next = fixedset_entry_n(h, idx+i);
    if (next->jump < 2) break;
    memcpy(e->kv, next->kv, h->ksize);
    e->jump = next->jump - 1;
    e = next;
  }
  e->jump = 0;
  return true;
}

size_t fixedset_size(fixedset_t* h) {
  return h->size;
}

size_t fixedset_capacity(fixedset_t* h) {
  return h->cap;
}

void fixedset_clear(fixedset_t* h) {
  memset(h->block, 0, h->cap * fixedset_entry_size(h));
  h->size = 0;
}
