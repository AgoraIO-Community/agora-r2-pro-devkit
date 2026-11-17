/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_CONTROLLER_H
#define RV1126B_CONTROLLER_H

#include "config.h"
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One product lifecycle owner on the calling thread. Borrowed settings, SDK
 * configuration and signal flag must outlive this call. Runtime guards, storage
 * and assets environment are prepared by main first. Owns UI/input/Wi-Fi/time/
 * prompt/SDK startup and orderly cleanup; returns 0 on requested exit, 1 on
 * failure. Incomplete SDK cleanup terminates without running explicit resource
 * cleanup so the supervisor can quarantine the instance. */
int rv1126b_controller_run(const rv1126b_app_config_t *settings, const mybot_config_t *config,
                           volatile sig_atomic_t *exit_requested);

#ifdef __cplusplus
}
#endif

#endif
