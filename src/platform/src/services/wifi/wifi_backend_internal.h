/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_WIFI_BACKEND_INTERNAL_H
#define RV1126B_WIFI_BACKEND_INTERNAL_H

#include "wifi_backend.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_COMMAND_OUTPUT_LIMIT (1024U * 1024U)

typedef struct {
    pid_t pid;
    int status;
} wifi_child_t;

typedef struct {
    int descriptor;
    char local[108];
} wifi_control_t;

typedef struct {
    char hw_mode;
    int channel;
} wifi_ap_radio_plan_t;

bool wifi_valid_interface(const char *name);

bool wifi_valid_ssid(const char *ssid);

bool wifi_hex_psk(const char *psk);

bool wifi_encode_hex(const void *bytes, size_t length, char *output, size_t capacity);

bool wifi_decode_hex(const char *text, char *output, size_t capacity, size_t *length);

bool wifi_decode_wpa(const char *text, char *output, size_t capacity);

void wifi_parse_scan_results(const char *text, wifi_scan_t *scan);

int wifi_frequency_channel(int frequency);

bool wifi_wiphy_from_interface_info(const char *text, char *output, size_t capacity);

bool wifi_managed_ap_multiple_channels(const char *text);

bool wifi_select_ap_radio(int frequency, bool multiple, wifi_ap_radio_plan_t *plan, char *error);

bool wifi_network_id_for_ssid(const char *list, const char *ssid, char output[9]);

bool wifi_same_association_lease(const char *initial, const char *current, const char *ssid,
                                 const char *trial, const char *initial_route,
                                 const char *current_route);

bool wifi_ensure_committed_selection(const char *status, const char *networks, const char *ssid,
                                     bool (*send)(void *, const char *), void *context,
                                     bool *reassociated, char *error);

bool wifi_read_credentials(int directory, wifi_credentials_t *credentials, char *error);

bool wifi_write_credentials(int directory, const wifi_credentials_t *credentials, char *error);

bool wifi_atomic_private_file(int directory, const char *name, const char *contents, char *error);

bool wifi_private_directory(const char *path, int *descriptor, char *error);

bool wifi_read_text(const char *path, wifi_buffer_t *output);

bool wifi_find_program(const char *program, char *path, size_t capacity);

bool wifi_process_identity_uses_interface(const char *program, const char *interface_name,
                                          const char *executable, const char *comm,
                                          const char *const *arguments);

bool wifi_process_uses_interface(const char *program, const char *interface_name);

void wifi_child_init(wifi_child_t *child);

bool wifi_child_start(wifi_child_t *child, const char *const *arguments, int output,
                      const char *const *environment, char *error);

bool wifi_child_running(wifi_child_t *child);

bool wifi_child_succeeded(const wifi_child_t *child);

void wifi_child_stop(wifi_child_t *child);

bool wifi_command(const char *const *arguments, unsigned timeout_ms, const wifi_cancel_t *cancel,
                  wifi_buffer_t *output, char *error);

void wifi_control_init(wifi_control_t *control);

void wifi_control_close(wifi_control_t *control);

bool wifi_control_open(wifi_control_t *control, const char *remote, const char *local);

bool wifi_control_request(wifi_control_t *control, const char *command, unsigned timeout_ms,
                          wifi_buffer_t *response);

bool wifi_control_ok(wifi_control_t *control, const char *command);

bool wifi_reconfigure_control(const char *const *remotes, const char *local, unsigned timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
