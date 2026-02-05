/*
   SipHash reference C implementation
   Copyright (c) 2012-2021 Jean-Philippe Aumasson
   <jeanphilippe.aumasson@gmail.com> Copyright (c) 2012 Daniel J. Bernstein
   <djb@cr.yp.to>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.
   You should have received a copy of the CC0 Public Domain Dedication along
   with this software. If not, see
   <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "halfsiphash.h"
#include "vectors.h"
#include "util/tests.h"
#include "util/error.h"
#include "util/log.h"

#define PRINTHASH(n)                                                            \
    TEST_LOG("    { ");                                                         \
    for (int j = 0; j < n; ++j) {                                               \
        TEST_LOG("0x%02x, ", out[j]);                                           \
    }                                                                           \
    TEST_LOG("},\n");

const char *functions[2] = {
    "const uint8_t vectors_hsip32[64][4] =",
    "const uint8_t vectors_hsip64[64][8] =",
};

const char *labels[2] = {
    "HalfSipHash-2-4-32",
    "HalfSipHash-2-4-64",
};

size_t lengths[2] = {4, 8};

int siphash_test() {
    uint8_t in[64], out[16], k[16];
    int i;

    for (i = 0; i < 16; ++i)
        k[i] = i;

    for (int version = 0; version < 2; ++version) {
        TEST_LOG("%s\n", labels[version]);

        for (i = 0; i < 64; ++i) {
            in[i] = i;
            int len = lengths[version];
            halfsiphash(in, i, k, out, len);
            const uint8_t *v = NULL;
            switch (version) {
            case 0:
                v = (uint8_t *)vectors_hsip32;
                break;
            case 1:
                v = (uint8_t *)vectors_hsip64;
                break;
            default:
                break;
            }

            TEST_ASSERT(memcmp(out, v + (i * len), len) == 0);
        }
    }

    return err_SUCCESS;
}

int main(void) {
    RUN_TEST(siphash_test());
    return 0;
}
