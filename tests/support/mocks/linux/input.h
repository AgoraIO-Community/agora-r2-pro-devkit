/* SPDX-License-Identifier: Apache-2.0 */
/* Linux event ABI constants for native tests; production uses target UAPI headers. */
#ifndef RV1126B_TEST_INPUT_H
#define RV1126B_TEST_INPUT_H
#include <stdint.h>
#include <sys/time.h>
#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define SYN_REPORT 0
#define SYN_DROPPED 3
#define KEY_ESC 1
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_VOLUMEDOWN 114
#define KEY_VOLUMEUP 115
#define KEY_POWER 116
#define KEY_MENU 139
#define KEY_MAX 0x2ff
#define BTN_TOUCH 330
#define ABS_MT_SLOT 47
#define ABS_MT_TRACKING_ID 57
#define EVIOCGVERSION 0x80044501UL
#define EVIOCGNAME(length) (0x80004506UL | ((unsigned long)(length) << 16))
struct input_event {
    struct timeval time;
    uint16_t type, code;
    int32_t value;
};
#endif
