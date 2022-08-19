
// Copyright 2022 Signal Messenger, LLC
// SPDX-License-Identifier: AGPL-3.0-only

#define ENCLAVE_TEST_ASSERT(x)                                                    \
    do                                                                            \
    {                                                                             \
        if (!(x))                                                                 \
        {                                                                         \
            fprintf(stderr, "  FAILED at %s:%d\n\t%s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

#define ENCLAVE_TEST_ERR(x)                                                                         \
    do                                                                                              \
    {                                                                                               \
        int _x = (x);                                                                               \
        if (_x != err_SUCCESS)                                                                      \
        {                                                                                           \
            fprintf(stderr, "  FAILED at %s:%d\n\t%s with code: %d\n", __FILE__, __LINE__, #x, _x); \
            exit(1);                                                                                \
        }                                                                                           \
    } while (0)

#define OPEN_ENCLAVE_CALL_TEST_ERR(x)                                                                                       \
    do                                                                                                                      \
    {                                                                                                                       \
        oe_result_t _x = (x);                                                                                               \
        if (_x != 0)                                                                                                        \
        {                                                                                                                   \
            fprintf(stderr, "  FAILED at %s:%d\n\t%s\nOE result=%u (%s)\n", __FILE__, __LINE__, #x, _x, oe_result_str(_x)); \
            exit(1);                                                                                                        \
        }                                                                                                                   \
    } while (0)
