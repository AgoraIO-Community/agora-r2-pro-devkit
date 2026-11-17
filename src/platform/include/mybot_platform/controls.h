/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PLATFORM_CONTROLS_H
#define MYBOT_PLATFORM_CONTROLS_H

#include <mybot_platform/platform.h>
#include <mybot_ui/settings.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Product input outlives the SDK, including network setup and first binding. */
int rv1126b_input_start(const rv1126b_platform_config_t *config, unsigned rotation);

int rv1126b_input_stop(void);

void rv1126b_input_settings_page(mybot_settings_page_t page);

/* Consumes one confirmed request. Application lifecycle performs shutdown. */
int rv1126b_key_poweroff_requested(void);

#ifdef __cplusplus
}
#endif

#endif
