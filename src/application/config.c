/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "config.h"
#include "board_config.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *environment(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return value ? value : fallback;
}

static int unsigned_value(const char *value, unsigned long max, unsigned int *output) {
    if (!value[0] || strspn(value, "0123456789") != strlen(value))
        return -1;
    errno = 0;
    char *end;
    unsigned long number = strtoul(value, &end, 10);
    if (errno || *end || number > max)
        return -1;
    *output = (unsigned int)number;
    return 0;
}

static void usage(const char *program) {
    printf(
        "Usage: %s [options]\n"
        "  --server HTTPS_URL       default: https://botstation.shengwang.cn/api\n"
        "  --device-id ID           explicit identity, at most 32 characters\n"
        "  --identity-interface IF  derive from permanent factory MAC; default: CPU Serial HMAC\n"
        "  --capture-device PCM     default: botstation_capture\n"
        "  --playback-device PCM    default: botstation_playback\n"
        "  --interface IF           network interface filter (default: routed IPv4)\n"
        "  --wifi-interface IF      Wi-Fi station interface (default: auto)\n"
        "  --wifi-provision         enter AP Wi-Fi setup before starting services\n"
        "  --no-wifi-provisioning   monitor existing networking without AP setup\n"
        "  --time-sync-timeout N    clock readiness timeout, 1..300 seconds (default: 30)\n"
        "  --time-sync-helper PATH optional absolute one-shot helper; default: system service\n"
        "  --input-device PATH      auto, none, stdin, or /dev/input/eventN\n"
        "  --touch-device PATH      auto, none, or /dev/input/eventN\n"
        "  --talk-key CODE          default: 139 (KEY_MENU); left/right adjust volume\n"
        "  --drm-device PATH        default: /dev/dri/card0\n"
        "  --rotation DEGREES       0/90/180/270, default: 270\n"
        "  --no-ui                  console status; audio/network remain enabled\n"
        "  --ui-demo                cycle 23 expressions without SDK, network or audio\n"
        "  --run-seconds N          exit after N seconds (0: until Ctrl-C)\n"
        "  --help / --version       no hardware access or file creation\n"
        "Environment: RV1126B_SERVER, RV1126B_DEVICE_ID, RV1126B_INTERFACE,\n"
        "RV1126B_INPUT_DEVICE, RV1126B_TOUCH_DEVICE, MYBOT_KV_STORE_DIR,\n"
        "RV1126B_WIFI_INTERFACE, MYBOT_ASSETS_DIR, MYBOT_LOCALE,\n"
        "RV1126B_TIME_SYNC_TIMEOUT, RV1126B_TIME_SYNC_HELPER;\n"
        "stdin s/q/p/u/d/w/e then Enter; hold touch/menu 3 s for Wi-Fi setup.\n",
        program);
}

int rv1126b_app_config_parse(int argc, char **argv, rv1126b_app_config_t *out) {
    if (!out || argc < 1 || !argv || !argv[0])
        return -1;
#if defined(__APPLE__)
    optreset = 1;
    optind = 1;
#else
    optind = 0;
#endif
    rv1126b_platform_config_t platform = {
        .capture_device = environment("RV1126B_CAPTURE_DEVICE", RV1126B_BOARD_CAPTURE),
        .playback_device = environment("RV1126B_PLAYBACK_DEVICE", RV1126B_BOARD_PLAYBACK),
        .network_interface = environment("RV1126B_INTERFACE", ""),
        .input_device = environment("RV1126B_INPUT_DEVICE", "auto"),
        .touch_device = environment("RV1126B_TOUCH_DEVICE", "auto"),
        .talk_key = RV1126B_BOARD_TALK_KEY,
        .enable_ui = true,
    };
    const char *server = environment("RV1126B_SERVER", "https://botstation.shengwang.cn/api");
    const char *device_id = environment("RV1126B_DEVICE_ID", "");
    const char *identity_interface = "";
    const char *drm = RV1126B_BOARD_DRM;
    unsigned int rotation = RV1126B_BOARD_ROTATION, seconds = 0;
    bool demo = false;
    bool wifi_provisioning = true, force_wifi_provisioning = false;
    const char *wifi_interface = environment("RV1126B_WIFI_INTERFACE", "");
    const char *time_timeout_text = environment("RV1126B_TIME_SYNC_TIMEOUT", "30");
    const char *time_helper = environment("RV1126B_TIME_SYNC_HELPER", "");
    enum {
        OPT_SERVER = 1000,
        OPT_ID,
        OPT_ID_INTERFACE,
        OPT_CAPTURE,
        OPT_PLAYBACK,
        OPT_INTERFACE,
        OPT_WIFI_INTERFACE,
        OPT_WIFI_PROVISION,
        OPT_NO_WIFI_PROVISION,
        OPT_TIME_TIMEOUT,
        OPT_TIME_HELPER,
        OPT_INPUT,
        OPT_TOUCH,
        OPT_KEY,
        OPT_DRM,
        OPT_ROTATION,
        OPT_NO_UI,
        OPT_DEMO,
        OPT_SECONDS,
        OPT_HELP,
        OPT_VERSION
    };
    static const struct option options[] = {
        {"server", required_argument, NULL, OPT_SERVER},
        {"device-id", required_argument, NULL, OPT_ID},
        {"identity-interface", required_argument, NULL, OPT_ID_INTERFACE},
        {"capture-device", required_argument, NULL, OPT_CAPTURE},
        {"playback-device", required_argument, NULL, OPT_PLAYBACK},
        {"interface", required_argument, NULL, OPT_INTERFACE},
        {"wifi-interface", required_argument, NULL, OPT_WIFI_INTERFACE},
        {"wifi-provision", no_argument, NULL, OPT_WIFI_PROVISION},
        {"no-wifi-provisioning", no_argument, NULL, OPT_NO_WIFI_PROVISION},
        {"time-sync-timeout", required_argument, NULL, OPT_TIME_TIMEOUT},
        {"time-sync-helper", required_argument, NULL, OPT_TIME_HELPER},
        {"input-device", required_argument, NULL, OPT_INPUT},
        {"touch-device", required_argument, NULL, OPT_TOUCH},
        {"talk-key", required_argument, NULL, OPT_KEY},
        {"drm-device", required_argument, NULL, OPT_DRM},
        {"rotation", required_argument, NULL, OPT_ROTATION},
        {"no-ui", no_argument, NULL, OPT_NO_UI},
        {"ui-demo", no_argument, NULL, OPT_DEMO},
        {"run-seconds", required_argument, NULL, OPT_SECONDS},
        {"help", no_argument, NULL, OPT_HELP},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0}};
    int option;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (option) {
        case OPT_SERVER:
            server = optarg;
            break;
        case OPT_ID:
            device_id = optarg;
            break;
        case OPT_ID_INTERFACE:
            identity_interface = optarg;
            break;
        case OPT_CAPTURE:
            platform.capture_device = optarg;
            break;
        case OPT_PLAYBACK:
            platform.playback_device = optarg;
            break;
        case OPT_INTERFACE:
            platform.network_interface = optarg;
            break;
        case OPT_WIFI_INTERFACE:
            wifi_interface = optarg;
            break;
        case OPT_WIFI_PROVISION:
            force_wifi_provisioning = true;
            break;
        case OPT_NO_WIFI_PROVISION:
            wifi_provisioning = false;
            break;
        case OPT_TIME_TIMEOUT:
            time_timeout_text = optarg;
            break;
        case OPT_TIME_HELPER:
            time_helper = optarg;
            break;
        case OPT_INPUT:
            platform.input_device = optarg;
            break;
        case OPT_TOUCH:
            platform.touch_device = optarg;
            break;
        case OPT_KEY:
            if (unsigned_value(optarg, KEY_MAX, &platform.talk_key) < 0)
                goto invalid;
            break;
        case OPT_DRM:
            drm = optarg;
            break;
        case OPT_ROTATION:
            if (unsigned_value(optarg, 270, &rotation) < 0 || rotation % 90)
                goto invalid;
            break;
        case OPT_NO_UI:
            platform.enable_ui = false;
            break;
        case OPT_DEMO:
            demo = true;
            break;
        case OPT_SECONDS:
            if (unsigned_value(optarg, 86400, &seconds) < 0)
                goto invalid;
            break;
        case OPT_HELP:
            usage(argv[0]);
            return 1;
        case OPT_VERSION:
            printf("mybot-rv1126b %s, MyBot 1.3.0, video=%d, UI=%s\n", RV1126B_VERSION,
                   MYBOT_ENABLE_VIDEO, RV1126B_LOCALE);
            return 1;
        default:
            goto invalid;
        }
    }
    if (optind != argc || (demo && !platform.enable_ui))
        goto invalid;
    unsigned int time_timeout;
    if (unsigned_value(time_timeout_text, 300, &time_timeout) < 0 || time_timeout == 0 ||
        (time_helper[0] && time_helper[0] != '/') || strlen(time_helper) >= PATH_MAX ||
        strpbrk(time_helper, "\r\n"))
        goto invalid;
    if (strlen(wifi_interface) >= IFNAMSIZ ||
        strspn(wifi_interface,
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            strlen(wifi_interface) ||
        (!wifi_provisioning && force_wifi_provisioning) || (demo && force_wifi_provisioning))
        goto invalid;
    if (!platform.talk_key || platform.talk_key == KEY_VOLUMEUP ||
        platform.talk_key == KEY_VOLUMEDOWN || platform.talk_key == KEY_LEFT ||
        platform.talk_key == KEY_RIGHT)
        goto invalid;
    *out = (rv1126b_app_config_t){
        .platform = platform,
        .server = server,
        .device_id = device_id,
        .identity_interface = identity_interface,
        .drm_device = drm,
        .rotation = rotation,
        .seconds = seconds,
        .demo = demo,
        .wifi_provisioning = wifi_provisioning,
        .force_wifi_provisioning = force_wifi_provisioning,
        .wifi_interface = wifi_interface,
        .time_sync_timeout = time_timeout,
        .time_sync_helper = time_helper,
    };
    return 0;
invalid:
    fprintf(stderr, "Invalid or oversized option; run --help\n");
    return -1;
}
