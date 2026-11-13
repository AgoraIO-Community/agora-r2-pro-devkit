/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_platform.h"
#include "rv1126b_lvgl_ui.h"
#include "device_config.h"
#include "version.h"

#include <api/aosl_log.h>
#include <errno.h>
#include <limits.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t s_exit;
static botstation_file_config_t s_file_config;

static void signal_handler(int signal_number) {
    (void)signal_number;
    s_exit = 1;
}

static const char *env_or(const char *key, const char *fallback) {
    const char *value = getenv(key);
    return value ? value : botstation_config_get(&s_file_config, key, fallback);
}

static int copy_field(char *dest, size_t size, const char *value, const char *name) {
    if (strlen(value) >= size) {
        fprintf(stderr, "%s exceeds %zu bytes\n", name, size - 1);
        return -1;
    }
    memcpy(dest, value, strlen(value) + 1);
    return 0;
}

static int wait_for_system_time(const struct timespec *interval) {
    time_t minimum_time = (time_t)BOTSTATION_MIN_WALL_TIME;
    unsigned int ticks = 0;
    bool waiting = false;
    while (!s_exit) {
        time_t now = time(NULL);
        if (now == (time_t)-1) {
            perror("cannot read system time");
            return -1;
        }
        if (now >= minimum_time) {
            AOSL_LOG_NTC("system time sanity check passed (epoch=%lld minimum=%lld); starting SDK",
                         (long long)now, (long long)minimum_time);
            if (waiting)
                AOSL_LOG_NTC("boot clock corrected; first READY request is still available");
            return 1;
        }
        if (ticks++ % 300 == 0) {
            AOSL_LOG_WRN(
                "waiting for system time correction before HTTPS "
                "(epoch=%lld minimum=%lld); check network time service or set the correct date",
                (long long)now, (long long)minimum_time);
        }
        if (!waiting) {
            rv1126b_lvgl_ui_set_wait(RV1126B_UI_WAIT_TIME);
        }
        waiting = true;
        nanosleep(interval, NULL);
    }
    return 0;
}

static void usage(const char *program) {
    printf("Usage: %s [--server HTTPS_URL --device-id ID] [options]\n"
           "Defaults: /etc/botstation-device.conf; environment and CLI override the file.\n"
           "Empty device ID: RV1126B-<Serial> from /proc/cpuinfo.\n"
           "BOTSTATION_ENABLED controls only the init script, not foreground execution.\n"
           "  --capture-device ALSA_PCM   default: BOTSTATION_CAPTURE_DEVICE or default\n"
           "  --playback-device ALSA_PCM  default: BOTSTATION_PLAYBACK_DEVICE or default\n"
           "  --interface NAME           monitor this interface (default: routed IPv4 interfaces)\n"
           "  --input-device PATH        Linux input device; omitted uses stdin\n"
           "  --talk-key NUMBER          Linux key code (default: 164, KEY_PLAYPAUSE)\n"
           "  --fw-ver VERSION           reported firmware version\n"
           "  --hw-model MODEL           reported hardware model\n"
           "  --version                  show the service version and Git revision\n"
           "  --help                     show this help\n"
           "Environment: BOTSTATION_SERVER, BOTSTATION_DEVICE_ID, BOTSTATION_INTERFACE,\n"
           "BOTSTATION_INPUT_DEVICE, BOTSTATION_TALK_KEY, BOTSTATION_VIDEO_DEVICE,\n"
           "MYBOT_KV_STORE_DIR,\n"
           "MYBOT_ASSETS_DIR, MYBOT_LOCALE.\n"
           "Stdin: s=start, q=stop, p=re-pair, u/d=volume, e=exit (then Enter).\n",
           program);
}

int main(int argc, char **argv) {
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
        fprintf(stderr, "Cannot configure line-buffered device logs\n");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("botstation-device %s (%s)\n", BOTSTATION_VERSION, BOTSTATION_GIT_REVISION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }
    if (botstation_config_load("/etc/botstation-device.conf", &s_file_config) < 0) {
        return 1;
    }
    mybot_config_t config = {0};
    rv1126b_platform_config_t platform = {
        .capture_device = env_or("BOTSTATION_CAPTURE_DEVICE", "default"),
        .playback_device = env_or("BOTSTATION_PLAYBACK_DEVICE", "default"),
        .network_interface = env_or("BOTSTATION_INTERFACE", ""),
        .input_device = env_or("BOTSTATION_INPUT_DEVICE", ""),
        .talk_key = KEY_PLAYPAUSE,
    };
    const char *server = env_or("BOTSTATION_SERVER", "");
    const char *device_id = env_or("BOTSTATION_DEVICE_ID", "");
    const char *firmware = env_or("BOTSTATION_FIRMWARE_VERSION", BOTSTATION_VERSION);
    const char *model = env_or("BOTSTATION_HARDWARE_MODEL", "rv1126b-r2pro");
    const char *talk_key = env_or("BOTSTATION_TALK_KEY", "164");
    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        if (strcmp(option, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (i + 1 == argc) {
            fprintf(stderr, "Missing value for %s\n", option);
            return 1;
        }
        const char *value = argv[++i];
        if (strcmp(option, "--server") == 0)
            server = value;
        else if (strcmp(option, "--device-id") == 0)
            device_id = value;
        else if (strcmp(option, "--fw-ver") == 0)
            firmware = value;
        else if (strcmp(option, "--hw-model") == 0)
            model = value;
        else if (strcmp(option, "--capture-device") == 0)
            platform.capture_device = value;
        else if (strcmp(option, "--playback-device") == 0)
            platform.playback_device = value;
        else if (strcmp(option, "--interface") == 0)
            platform.network_interface = value;
        else if (strcmp(option, "--input-device") == 0)
            platform.input_device = value;
        else if (strcmp(option, "--talk-key") == 0)
            talk_key = value;
        else {
            fprintf(stderr, "Unknown option: %s\n", option);
            return 1;
        }
    }
    char *end = NULL;
    errno = 0;
    unsigned long code = strtoul(talk_key, &end, 10);
    if (errno || !talk_key[0] || *end || code == KEY_RESERVED || code > KEY_MAX ||
        code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN) {
        fprintf(stderr, "Invalid --talk-key: %s\n", talk_key);
        return 1;
    }
    platform.talk_key = (unsigned int)code;
    char generated_device_id[32];
    if (!device_id[0]) {
        FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
        if (!cpuinfo) {
            perror("Cannot open /proc/cpuinfo");
            return 1;
        }
        int result =
            botstation_device_id_read(cpuinfo, generated_device_id, sizeof(generated_device_id));
        if (fclose(cpuinfo) != 0) {
            perror("Cannot close /proc/cpuinfo");
            return 1;
        }
        if (result < 0) {
            return 1;
        }
        device_id = generated_device_id;
        printf("BotStation: device ID from CPU Serial: %s\n", device_id);
    }
    if (strncmp(server, "https://", 8) != 0 || !server[8]) {
        fprintf(stderr, "A device-service HTTPS URL and unique device ID are required\n");
        return 1;
    }
    if (strlen(device_id) > 32) {
        fprintf(stderr, "BotStation device ID must not exceed 32 bytes\n");
        return 1;
    }
    if (copy_field(config.server_base, sizeof(config.server_base), server, "server") < 0 ||
        copy_field(config.device_id, sizeof(config.device_id), device_id, "device-id") < 0 ||
        copy_field(config.firmware_ver, sizeof(config.firmware_ver), firmware, "fw-ver") < 0 ||
        copy_field(config.hw_model, sizeof(config.hw_model), model, "hw-model") < 0) {
        return 1;
    }
    if (setenv("MYBOT_KV_STORE_DIR", env_or("MYBOT_KV_STORE_DIR", "/userdata/botstation"), 0) < 0 ||
        setenv("MYBOT_ASSETS_DIR", env_or("MYBOT_ASSETS_DIR", "/usr/share/botstation"), 0) < 0 ||
        setenv("MYBOT_LOCALE", env_or("MYBOT_LOCALE", "zh-CN"), 0) < 0 ||
        setenv("BOTSTATION_VIDEO_DEVICE", env_or("BOTSTATION_VIDEO_DEVICE", ""), 0) < 0 ||
        (env_or("MYBOT_LCD_COLOR", NULL) &&
         setenv("MYBOT_LCD_COLOR", env_or("MYBOT_LCD_COLOR", NULL), 0) < 0)) {
        perror("setenv");
        return 1;
    }
    umask(0077);
    if (!getenv("MYBOT_KV_STORE_DIR")[0] || strlen(getenv("MYBOT_ASSETS_DIR")) >= 160 ||
        strlen(env_or("MYBOT_LOCALE", "zh-CN")) >= 64) {
        fprintf(stderr, "Invalid KV directory or oversized announcement resource configuration\n");
        return 1;
    }
    int lock = open("/run/botstation-device.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
        perror("cannot acquire device-service lock");
        if (lock >= 0)
            close(lock);
        return 1;
    }
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        close(lock);
        return 1;
    }
    aosl_set_log_level(AOSL_LOG_NOTICE);
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0) {
        perror("sigaction SIGPIPE");
        close(lock);
        return 1;
    }
    if (rv1126b_platform_register(&platform) < 0) {
        close(lock);
        return 1;
    }
    if (rv1126b_lvgl_ui_start() < 0) {
        AOSL_LOG_ERR("LVGL DRM display startup failed");
        close(lock);
        return 1;
    }
    AOSL_LOG_NTC("waiting for an IPv4 address and default route; provisioning is product-owned");
    rv1126b_lvgl_ui_set_wait(RV1126B_UI_WAIT_NETWORK);
    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 100000000};
    unsigned int ticks = 0;
    int ready = 0;
    while (!s_exit) {
        if (ticks++ % 10 == 0) {
            ready = rv1126b_network_ready(platform.network_interface);
            if (ready != 0)
                break;
        }
        nanosleep(&interval, NULL);
    }
    if (s_exit || ready < 0) {
        rv1126b_lvgl_ui_stop();
        close(lock);
        return ready < 0 ? 1 : 0;
    }
    /* An IP route can be ready before network time corrects the boot clock. */
    int time_ready = wait_for_system_time(&interval);
    if (s_exit || time_ready <= 0) {
        rv1126b_lvgl_ui_stop();
        close(lock);
        return time_ready < 0 ? 1 : 0;
    }
    rv1126b_lvgl_ui_set_wait(RV1126B_UI_WAIT_SERVICES);
    if (mybot_start(&config) < 0) {
        AOSL_LOG_ERR("device service startup failed");
        rv1126b_lvgl_ui_stop();
        close(lock);
        return 1;
    }
    while (!s_exit && mybot_is_running() && mybot_get_state() != MYBOT_STATE_FAILED) {
        nanosleep(&interval, NULL);
    }
    int failed =
        mybot_get_state() == MYBOT_STATE_FAILED || (!s_exit && !rv1126b_key_exit_requested());
    if (failed)
        AOSL_LOG_ERR("device service failed; shutting down");
    mybot_stop();
    if (mybot_get_state() != MYBOT_STATE_STOPPED) {
        AOSL_LOG_ERR("device service teardown did not complete");
        failed = 1;
    }
    rv1126b_lvgl_ui_stop();
    close(lock);
    return failed ? 1 : 0;
}
