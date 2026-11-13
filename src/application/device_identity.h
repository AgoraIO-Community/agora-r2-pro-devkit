/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_DEVICE_IDENTITY_H
#define RV1126B_DEVICE_IDENTITY_H
#include <stddef.h>
#include <stdio.h>
/* Stable product identity: RV1126B- + 96 bits of uppercase HMAC-SHA256. */
int rv1126b_identity_from_serial(FILE *cpuinfo, char *output, size_t capacity);
int rv1126b_identity_from_mac(const char *interface_name, char *output, size_t capacity);
#endif
