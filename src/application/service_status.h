/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_SERVICE_STATUS_H
#define RV1126B_SERVICE_STATUS_H

/* Product-thread-only publication. Unset MYBOT_STATUS_PATH disables all I/O.
 * The parent must be an owned private directory. No device identity or secrets
 * are included. Failure preserves the previous complete record. */
int rv1126b_service_status(const char *state);

#endif
