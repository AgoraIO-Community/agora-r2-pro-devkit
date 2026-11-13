/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_ASOUNDLIB_H
#define MYBOT_TEST_ASOUNDLIB_H

#include <alloca.h>
#include <errno.h>
#include <stddef.h>

/* Linux-only errno used by the production recovery path. */
#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif

typedef struct {
    int open;
} snd_pcm_t;
typedef struct {
    int unused;
} snd_pcm_hw_params_t;
typedef struct {
    unsigned long threshold;
} snd_pcm_sw_params_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
typedef int snd_pcm_stream_t;
enum {
    SND_PCM_STREAM_PLAYBACK,
    SND_PCM_STREAM_CAPTURE,
    SND_PCM_NONBLOCK = 1,
    SND_PCM_ACCESS_RW_INTERLEAVED = 3,
    SND_PCM_FORMAT_S16_LE = 2
};

#define snd_pcm_hw_params_alloca(out) (*(out) = alloca(sizeof(snd_pcm_hw_params_t)))
#define snd_pcm_sw_params_alloca(out) (*(out) = alloca(sizeof(snd_pcm_sw_params_t)))
int snd_pcm_open(snd_pcm_t **pcm, const char *device, snd_pcm_stream_t stream, int flags);
int snd_pcm_close(snd_pcm_t *pcm);
const char *snd_strerror(int error);
int snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
int snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int access);
int snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int format);
int snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned channels);
int snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned *rate,
                                    int *direction);
int snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                           snd_pcm_uframes_t *frames, int *direction);
int snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
                                           snd_pcm_uframes_t *frames);
int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
int snd_pcm_get_params(snd_pcm_t *pcm, snd_pcm_uframes_t *buffer, snd_pcm_uframes_t *period);
int snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
int snd_pcm_sw_params_set_avail_min(snd_pcm_t *pcm, snd_pcm_sw_params_t *params,
                                    snd_pcm_uframes_t frames);
int snd_pcm_sw_params_set_start_threshold(snd_pcm_t *pcm, snd_pcm_sw_params_t *params,
                                          snd_pcm_uframes_t frames);
int snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
int snd_pcm_sw_params_get_start_threshold(const snd_pcm_sw_params_t *params,
                                          snd_pcm_uframes_t *frames);
int snd_pcm_prepare(snd_pcm_t *pcm);
int snd_pcm_drop(snd_pcm_t *pcm);
snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t frames);
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t frames);
int snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *frames);
int snd_pcm_state(snd_pcm_t *pcm);
const char *snd_pcm_state_name(int state);

#endif
