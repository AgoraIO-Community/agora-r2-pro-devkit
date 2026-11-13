/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_DRM_H
#define RV1126B_UI_DRM_H
#include "pixels.h"
/* Single-owner DRM context; all calls belong to the UI thread. */
int rv1126b_drm_start(const char *device, unsigned rotation);
int rv1126b_drm_present(const uint32_t *canvas, const rv1126b_rect_t *area, void *unused);
int rv1126b_drm_stop(void);
#endif
