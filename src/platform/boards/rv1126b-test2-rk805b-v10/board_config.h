/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_BOARD_CONFIG_H
#define RV1126B_BOARD_CONFIG_H

/* Defaults for the verified firmware image, overridable by the existing CLI. */
#define RV1126B_BOARD_MODEL "rv1126b-test2-rk805b-v10"
#define RV1126B_BOARD_CAPTURE "botstation_capture"
#define RV1126B_BOARD_PLAYBACK "botstation_playback"
#define RV1126B_BOARD_DRM "/dev/dri/card0"
#define RV1126B_BOARD_ROTATION 270
#define RV1126B_BOARD_TALK_KEY 139
#define RV1126B_BOARD_SCREEN_WIDTH 480
#define RV1126B_BOARD_SCREEN_HEIGHT 640
#define RV1126B_BOARD_VIDEO_WIDTH 640
#define RV1126B_BOARD_VIDEO_HEIGHT 480
#define RV1126B_BOARD_VIDEO_FPS 3
#define RV1126B_BOARD_MPP_ROTATION MPP_ENC_ROT_180

#endif
