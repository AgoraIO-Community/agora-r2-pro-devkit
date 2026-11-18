/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_FACTORY_RESET_H
#define RV1126B_FACTORY_RESET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 when a reset requires fresh AP provisioning, 0 for an ordinary
 * startup and -1 if the persistent state cannot be verified. */
int rv1126b_factory_setup_pending(void);

/* Removes the durable setup marker after a newly submitted network is saved
 * and verified online. Returns 0 on success (including already cleared). */
int rv1126b_factory_setup_complete(void);

#ifdef __cplusplus
}
#endif
#endif
