/* SPDX-License-Identifier: Apache-2.0 */
#include "device_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static botstation_file_config_t s_config;

static int parse(const char *text) {
    FILE *file = tmpfile();
    CHECK(file != NULL);
    CHECK(fwrite(text, 1, strlen(text), file) == strlen(text));
    rewind(file);
    int result = botstation_config_read(file, "test-config", &s_config);
    CHECK(fclose(file) == 0);
    return result;
}

static void check_serial(const char *text, const char *expected) {
    FILE *file = tmpfile();
    char id[32] = {0};
    CHECK(file != NULL);
    CHECK(fwrite(text, 1, strlen(text), file) == strlen(text));
    rewind(file);
    int result = botstation_device_id_read(file, id, sizeof(id));
    CHECK(fclose(file) == 0);
    if (expected) {
        CHECK(result == 0);
        CHECK(strcmp(id, expected) == 0);
    } else {
        CHECK(result < 0);
    }
}

int main(int argc, char **argv) {
    check_serial("processor : 0\nSerial\t: 72bedba03e672ad9\n", "RV1126B-72BEDBA03E672AD9");
    check_serial(" Serial : 72BEDBA03E672AD9 \r\n", "RV1126B-72BEDBA03E672AD9");
    check_serial("Serial:72bedba03e672ad9", "RV1126B-72BEDBA03E672AD9");
    check_serial("processor : 0\n", NULL);
    check_serial("Serial : 0000000000000000\n", NULL);
    check_serial("Serial : ffffffffffffffff\n", NULL);
    check_serial("Serial : 72bedba03e672adz\n", NULL);
    check_serial("Serial : 72bedba03e672ad\n", NULL);
    check_serial("Serial : 172bedba03e672ad9\n", NULL);
    check_serial("Serial : 72bedba03e672ad9\nSerial : 72bedba03e672ad9\n", NULL);
    CHECK(parse("# comment\r\n\n"
                "BOTSTATION_ENABLED=0\r\n"
                "BOTSTATION_SERVER='https://botstation.shengwang.cn/api'\r\n"
                "BOTSTATION_DEVICE_ID=\"RV1126B-2289846A96AB\" # comment\n"
                "BOTSTATION_INPUT_DEVICE=''\n"
                "BOTSTATION_TALK_KEY = 164\n"
                "MYBOT_KV_STORE_DIR=/userdata/botstation\n"
                "MYBOT_LOCALE='zh-CN'") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_SERVER", ""),
                 "https://botstation.shengwang.cn/api") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_DEVICE_ID", ""),
                 "RV1126B-2289846A96AB") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_TALK_KEY", ""), "164") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_INPUT_DEVICE", "stdin"), "") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_VIDEO_DEVICE", "auto"), "auto") == 0);
    CHECK(parse("BOTSTATION_VIDEO_DEVICE=''\n") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_VIDEO_DEVICE", "auto"), "") == 0);
    CHECK(parse("BOTSTATION_VIDEO_DEVICE='/dev/video11'\n") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_VIDEO_DEVICE", ""), "/dev/video11") ==
          0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_CAPTURE_DEVICE", "default"),
                 "default") == 0);
    CHECK(parse("BOTSTATION_SERVER='first'\nBOTSTATION_SERVER='second'\n") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_SERVER", ""), "second") == 0);
    CHECK(parse("UNKNOWN=value\n") < 0);
    CHECK(parse("BOTSTATION_SERVER='unterminated\n") < 0);
    CHECK(parse("BOTSTATION_SERVER=\"$(echo bad)\"\n") < 0);
    CHECK(parse("BOTSTATION_SERVER='ok'; echo bad\n") < 0);
    CHECK(parse("BOTSTATION_SERVER=bad;command\n") < 0);
    CHECK(parse("BOTSTATION_SERVER=bad`command`\n") < 0);
    CHECK(parse("BOTSTATION_VIDEO_DEVICE=\"$(echo bad)\"\n") < 0);
    char oversized[600];
    memset(oversized, 'a', sizeof(oversized));
    memcpy(oversized, "BOTSTATION_SERVER=", 18);
    oversized[sizeof(oversized) - 1] = '\0';
    CHECK(parse(oversized) < 0);
    CHECK(parse("BOTSTATION_SERVER=''\n") == 0);
    CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_SERVER", "fallback"), "") == 0);
    if (argc == 2) {
        CHECK(botstation_config_load(argv[1], &s_config) == 0);
        CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_SERVER", ""),
                     "https://botstation.shengwang.cn/api") == 0);
        CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_DEVICE_ID", ""), "") == 0);
        CHECK(strcmp(botstation_config_get(&s_config, "BOTSTATION_VIDEO_DEVICE", "missing"), "") ==
              0);
    }
    puts("BotStation configuration tests passed");
    return 0;
}
