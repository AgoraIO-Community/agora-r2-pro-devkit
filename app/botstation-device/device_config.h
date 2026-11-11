/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOTSTATION_DEVICE_CONFIG_H
#define BOTSTATION_DEVICE_CONFIG_H

#include <stdbool.h>
#include <stdio.h>

#define BOTSTATION_CONFIG_KEYS 15
#define BOTSTATION_CONFIG_VALUE_SIZE 512

typedef struct {
    bool present[BOTSTATION_CONFIG_KEYS];
    char values[BOTSTATION_CONFIG_KEYS][BOTSTATION_CONFIG_VALUE_SIZE];
} botstation_file_config_t;

int botstation_config_read(FILE *file, const char *path, botstation_file_config_t *config);
int botstation_config_load(const char *path, botstation_file_config_t *config);
const char *botstation_config_get(const botstation_file_config_t *config, const char *key,
                                 const char *fallback);
int botstation_device_id_read(FILE *file, char *device_id, size_t size);

#endif
