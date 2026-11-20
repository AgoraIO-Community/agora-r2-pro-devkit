/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_SUPERVISOR_H
#define MYBOT_SUPERVISOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A payload exits with this status when it cannot confirm SDK shutdown. */
#define RV1126B_EXIT_UNCLEAN 78

/* Standalone process supervision; no SDK, device, network or shell dependency.
 * A zero policy field selects the production default. argv must remain valid
 * until run returns. Production descendant adoption requires Linux /proc. */
typedef struct {
    const char *runtime_directory;
    const char *log_directory;
    char *const *argv;
    /* NULL disables poweroff. The command receives no arguments and executes
     * only after an authenticated request and verified clean application exit. */
    const char *poweroff_command;
    /* Private 0700 directory for the durable factory-reset.pending marker.
     * NULL disables reset requests. The launcher consumes the marker before
     * opening application data on startup, including after a power loss. */
    const char *reset_directory;
    unsigned int terminate_timeout_ms;
    unsigned int cleanup_timeout_ms;
    unsigned int restart_window_ms;
    unsigned int restart_delays_ms[3];
    size_t log_limit_bytes;
} rv1126b_supervisor_config_t;

int rv1126b_supervisor_run(const rv1126b_supervisor_config_t *config);

/* stop=0 queries status; stop=1 waits for complete owned-process cleanup.
 * Returns 0 on running/successful stop, 1 on failed, 3 when absent. */
int rv1126b_supervisor_control(const char *runtime_directory, int stop);

int rv1126b_supervisor_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
