/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#define AOSL_LOG_NTC(...)                                                                          \
    do {                                                                                           \
        if (0)                                                                                     \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while (0)
#define AOSL_LOG_WRN(...)                                                                          \
    do {                                                                                           \
        if (0)                                                                                     \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while (0)
#define AOSL_LOG_ERR(...)                                                                          \
    do {                                                                                           \
        if (0)                                                                                     \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while (0)
