/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PLATFORM_POWER_H
#define MYBOT_PLATFORM_POWER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Requests planned poweroff from this application's direct supervisor. The
 * bounded acknowledgement never waits for application cleanup. On success the
 * caller must release its resources and exit normally with status zero; the
 * supervisor cuts power only after verifying that cleanup. Returns zero when
 * accepted, -1 with errno on rejection, timeout or unavailable supervisor.
 * Manual launches without MYBOT_SUPERVISOR_RUNTIME cannot power off. */
int rv1126b_poweroff_request(void);

/* Returns 0 once the direct supervisor has persisted a confirmed reset.
 * The application should then shut down cleanly. The next supervised launch
 * resumes the pending reset before opening any SDK or network state. */
int rv1126b_factory_reset_request(void);

#ifdef __cplusplus
}
#endif

#endif
