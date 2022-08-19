// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#ifndef _CDSI_TEST_H
#define _CDSI_TEST_H

#ifdef IS_TEST
#include <stdlib.h>
#include <stdio.h>

#define TEST_STASH_SIZE 100

#define TEST_ASSERT(x)                                                            \
    do                                                                            \
    {                                                                             \
        if (!(x))                                                                 \
        {                                                                         \
            fprintf(stderr, "  FAILED at %s:%d\n\t%s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define TEST_ERR(x) TEST_ASSERT(err_SUCCESS == (x))

#define RUN_TEST(x)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "\nRunning test: %s...\n", #x); \
        int _result = x;                                \
        if (_result == 0)                               \
        {                                               \
            fprintf(stderr, "  SUCCESS\n");             \
        }                                               \
        else                                            \
        {                                               \
            fprintf(stderr, " FAIL (%d)\n", _result);   \
            exit(_result);                              \
        }                                               \
    } while (0)

#endif  // IS_TEST

#endif // _CDSI_TEST_H
