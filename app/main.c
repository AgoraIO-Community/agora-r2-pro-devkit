/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "device_identity.h"
#include "rv1126b_platform.h"
#include "rv1126b_ui.h"
#include "display/pixel_expression.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t s_exit;

static void signal_handler(int signal_number) {
    (void)signal_number;
    s_exit = 1;
}

static const char *environment(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return value ? value : fallback;
}

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
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

static int copy_field(char *output, size_t size, const char *input) {
    if (strlen(input) >= size)
        return -1;
    memcpy(output, input, strlen(input) + 1);
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
        "MYBOT_ASSETS_DIR, MYBOT_LOCALE; stdin s/q/p/u/d/e then Enter.\n",
        program);
}

static int lock_file(const char *path, bool create) {
    int fd = open(path, (create ? O_CREAT | O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return !create && errno == ENOENT ? -2 : -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || (create && st.st_uid != geteuid()) ||
        flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int legacy_service_running(void) {
    DIR *processes = opendir("/proc");
    if (!processes)
        return -1;
    int running = 0;
    struct dirent *entry;
    while ((entry = readdir(processes)) != NULL) {
        if (!entry->d_name[0] || strspn(entry->d_name, "0123456789") != strlen(entry->d_name))
            continue;
        char path[512], name[64];
        if (snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name) >= (int)sizeof(path))
            continue;
        FILE *file = fopen(path, "r");
        if (!file)
            continue;
        bool read = fgets(name, sizeof(name), file) != NULL;
        fclose(file);
        if (read && (!strcmp(name, "botstation-devi\n") || !strcmp(name, "botstation-device\n"))) {
            running = 1;
            break;
        }
    }
    closedir(processes);
    return running;
}

static int prepare_storage(const char *path) {
    if (path[0] != '/' || !path[1])
        return -1;
    if (!strncmp(path, "/userdata/", 10)) {
        FILE *mounts = fopen("/proc/mounts", "r");
        if (!mounts)
            return -1;
        char line[512], target[128];
        bool mounted = false;
        while (fgets(line, sizeof(line), mounts)) {
            if (sscanf(line, "%*s %127s", target) == 1 && !strcmp(target, "/userdata"))
                mounted = true;
        }
        int error = ferror(mounts);
        if (fclose(mounts) || error || !mounted) {
            fprintf(stderr, "/userdata must be mounted before private storage is created\n");
            return -1;
        }
    }
    if (mkdir(path, 0700) < 0 && errno != EEXIST)
        return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct stat st;
    int result =
        fstat(fd, &st) < 0 || st.st_uid != geteuid() || (st.st_mode & 0077) || fchdir(fd) < 0 ? -1
                                                                                              : 0;
    close(fd);
    return result;
}

int main(int argc, char **argv) {
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    rv1126b_platform_config_t platform = {
        .capture_device = environment("RV1126B_CAPTURE_DEVICE", "botstation_capture"),
        .playback_device = environment("RV1126B_PLAYBACK_DEVICE", "botstation_playback"),
        .network_interface = environment("RV1126B_INTERFACE", ""),
        .input_device = environment("RV1126B_INPUT_DEVICE", "auto"),
        .touch_device = environment("RV1126B_TOUCH_DEVICE", "auto"),
        .talk_key = KEY_MENU,
        .enable_ui = true,
    };
    const char *server = environment("RV1126B_SERVER", "https://botstation.shengwang.cn/api");
    const char *device_id = environment("RV1126B_DEVICE_ID", "");
    const char *identity_interface = "";
    const char *drm = "/dev/dri/card0";
    unsigned int rotation = 270, seconds = 0;
    bool demo = false;
    enum {
        OPT_SERVER = 1000,
        OPT_ID,
        OPT_ID_INTERFACE,
        OPT_CAPTURE,
        OPT_PLAYBACK,
        OPT_INTERFACE,
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
            return 0;
        case OPT_VERSION:
            printf("mybot-rv1126b %s, MyBot 1.3.0, video=%d, UI=%s\n", RV1126B_VERSION,
                   MYBOT_ENABLE_VIDEO, RV1126B_LOCALE);
            return 0;
        default:
            goto invalid;
        }
    }
    if (optind != argc || (demo && !platform.enable_ui))
        goto invalid;
    if (!platform.talk_key || platform.talk_key == KEY_VOLUMEUP ||
        platform.talk_key == KEY_VOLUMEDOWN || platform.talk_key == KEY_LEFT ||
        platform.talk_key == KEY_RIGHT)
        goto invalid;
    mybot_config_t config = {0};
    char generated_id[33];
    if (!demo) {
        if (strncmp(server, "https://", 8) || !server[8] || strlen(device_id) > 32)
            goto invalid;
        if (!device_id[0]) {
            int result;
            if (identity_interface[0])
                result = rv1126b_identity_from_mac(identity_interface, generated_id,
                                                   sizeof(generated_id));
            else {
                FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
                if (!cpuinfo) {
                    perror("CPU identity");
                    return 1;
                }
                result = rv1126b_identity_from_serial(cpuinfo, generated_id, sizeof(generated_id));
                if (fclose(cpuinfo) != 0)
                    result = -1;
            }
            if (result < 0) {
                fprintf(stderr, "Cannot derive stable identity; supply --device-id or a permanent "
                                "--identity-interface\n");
                return 1;
            }
            device_id = generated_id;
        }
        if (copy_field(config.server_base, sizeof(config.server_base), server) < 0 ||
            copy_field(config.device_id, sizeof(config.device_id), device_id) < 0 ||
            copy_field(config.firmware_ver, sizeof(config.firmware_ver), RV1126B_VERSION) < 0 ||
            copy_field(config.hw_model, sizeof(config.hw_model), "rv1126b-test2-rk805b-v10") < 0)
            goto invalid;
    }
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0)
        return 1;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0)
        return 1;
    umask(0077);
    if (legacy_service_running() != 0) {
        fprintf(stderr, "Existing botstation service is running, or /proc cannot be checked. Stop "
                        "that service before this test.\n");
        return 1;
    }
    int legacy_lock = lock_file("/run/botstation-device.lock", false);
    if (legacy_lock == -1) {
        fprintf(stderr, "Existing botstation service holds its lock (or it cannot be checked). "
                        "Stop it before this test; it owns the same hardware.\n");
        return 1;
    }
    int lock = lock_file("/run/mybot-rv1126b.lock", true);
    if (lock < 0) {
        fprintf(stderr,
                "Cannot acquire /run/mybot-rv1126b.lock; another instance may be running\n");
        if (legacy_lock >= 0)
            close(legacy_lock);
        return 1;
    }
    int failed = 0;
    bool sdk_started = false, ui_started = false;
    if (!demo) {
        char executable[PATH_MAX], assets[PATH_MAX];
        ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length <= 0 || (size_t)length >= sizeof(executable) - 1) {
            failed = 1;
            goto cleanup;
        }
        executable[length] = '\0';
        char *slash = strrchr(executable, '/');
        if (!slash) {
            failed = 1;
            goto cleanup;
        }
        *slash = '\0';
        if (snprintf(assets, sizeof(assets), "%s/../assets", executable) >= (int)sizeof(assets) ||
            setenv("MYBOT_ASSETS_DIR", assets, 0) < 0 ||
            setenv("MYBOT_KV_STORE_DIR", "/userdata/mybot-rv1126b", 0) < 0 ||
            setenv("MYBOT_LOCALE", RV1126B_LOCALE, 0) < 0 || getenv("MYBOT_ASSETS_DIR")[0] != '/' ||
            prepare_storage(getenv("MYBOT_KV_STORE_DIR")) < 0) {
            perror("Cannot prepare private runtime directory");
            failed = 1;
            goto cleanup;
        }
        fprintf(stdout, "mybot-rv1126b %s: id=%s, locale=%s, ptime=%d ms, video=%d\n",
                RV1126B_VERSION, device_id, RV1126B_LOCALE, MYBOT_AUDIO_PTIME_MS,
                MYBOT_ENABLE_VIDEO);
    }
    if (platform.enable_ui) {
        if (rv1126b_ui_start(drm, rotation) < 0) {
            failed = 1;
            goto cleanup;
        }
        ui_started = true;
    }
    uint64_t started = monotonic_ms();
    if (!started) {
        failed = 1;
        goto cleanup;
    }
    uint64_t deadline = seconds ? started + (uint64_t)seconds * 1000 : UINT64_MAX;
    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 100000000};
    if (demo) {
        const mybot_lcd_content_t content = {MYBOT_LCD_SCREEN_READY, {0}, 0};
        rv1126b_ui_set_state(MYBOT_STATE_READY);
        rv1126b_ui_show(&content);
        int last = -1;
        while (!s_exit && monotonic_ms() < deadline && !rv1126b_ui_failed()) {
            int expression = (int)(((monotonic_ms() - started) / 2000) % MYBOT_EXPRESSION_COUNT);
            if (expression != last) {
                rv1126b_ui_demo_expression(expression);
                fprintf(stdout, "UI demo: %s\n",
                        mybot_expression_name((mybot_expression_t)expression));
                last = expression;
            }
            nanosleep(&interval, NULL);
        }
        failed = rv1126b_ui_failed();
        goto cleanup;
    }
    if (rv1126b_platform_register(&platform) < 0) {
        fprintf(stderr, "Invalid platform configuration\n");
        failed = 1;
        goto cleanup;
    }
    const mybot_lcd_content_t waiting = {MYBOT_LCD_SCREEN_WIFI_PROVISIONING, {0}, 0};
    rv1126b_ui_set_state(MYBOT_STATE_WIFI_PROVISIONING);
    rv1126b_ui_show(&waiting);
    fprintf(stdout,
            "Waiting for usable IPv4 routing and system time; network setup is product-owned\n");
    while (!s_exit && monotonic_ms() < deadline) {
        int ready = rv1126b_network_ready(platform.network_interface);
        if (ready < 0 || (platform.enable_ui && rv1126b_ui_failed())) {
            failed = 1;
            goto cleanup;
        }
        if (ready && time(NULL) >= 1767225600)
            break;
        nanosleep(&interval, NULL);
    }
    if (s_exit || monotonic_ms() >= deadline)
        goto cleanup;
    if (mybot_start(&config) < 0) {
        fprintf(stderr, "MyBot startup failed\n");
        failed = 1;
        goto cleanup;
    }
    sdk_started = true;
    mybot_state_t previous = MYBOT_STATE_STOPPED;
    while (!s_exit && monotonic_ms() < deadline && mybot_is_running()) {
        mybot_state_t state = mybot_get_state();
        if (state != previous) {
            fprintf(stdout, "SDK state=%d\n", state);
            rv1126b_ui_set_state(state);
            previous = state;
        }
        if (state == MYBOT_STATE_FAILED || (platform.enable_ui && rv1126b_ui_failed())) {
            failed = 1;
            break;
        }
        nanosleep(&interval, NULL);
    }
    if (!s_exit && monotonic_ms() < deadline && !rv1126b_key_exit_requested())
        failed = 1;
cleanup:
    if (sdk_started) {
        mybot_stop();
        if (mybot_get_state() != MYBOT_STATE_STOPPED) {
            fprintf(stderr, "SDK shutdown incomplete; retrying cleanup\n");
            mybot_stop();
            if (mybot_get_state() != MYBOT_STATE_STOPPED) {
                fprintf(stderr, "SDK workers remain active; terminating process with locks held\n");
                _Exit(EXIT_FAILURE);
            }
        }
    }
    if (ui_started && rv1126b_ui_stop() < 0)
        failed = 1;
    close(lock);
    if (legacy_lock >= 0)
        close(legacy_lock);
    return failed ? 1 : 0;
invalid:
    fprintf(stderr, "Invalid or oversized option; run --help\n");
    return 2;
}
