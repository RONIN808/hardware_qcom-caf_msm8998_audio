/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "audio_hw_tfa98xx_feedback"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <dirent.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include "audio_extn.h"

static struct pcm *tfa98xx_out;

static struct pcm_config pcm_config_tfa98xx = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
};

// Check for device
static bool check_snd_device(snd_device_t snd_device)
{
    bool is_device = false;

    switch(snd_device) {
	case SND_DEVICE_OUT_SPEAKER:
	case SND_DEVICE_OUT_SPEAKER_PROTECTED:
	case SND_DEVICE_OUT_SPEAKER_REVERSE:
	case SND_DEVICE_OUT_SPEAKER_VBAT:
	case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
	case SND_DEVICE_OUT_SPEAKER_AND_LINE:
	case SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET:
	case SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET:
	case SND_DEVICE_OUT_VOICE_SPEAKER:
	case SND_DEVICE_OUT_VOICE_SPEAKER_2:
		is_device = true;
    }
    return is_device;
}


int audio_extn_tfa98xx_start_feedback(struct audio_device *adev, snd_device_t snd_device)
{
    struct audio_usecase *uc_info_rx;
    int rx_device_id = 0, retval = 0;

    if (!check_snd_device(snd_device)) {
        return 0;
    }

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    if (tfa98xx_out) {
        return 0;
    }

    uc_info_rx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_rx) {
        return -ENOMEM;
    }
    uc_info_rx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    uc_info_rx->type = PCM_CAPTURE;
    uc_info_rx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;

    list_add_tail(&adev->usecase_list, &uc_info_rx->list);
    enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    enable_audio_route(adev, uc_info_rx);

    rx_device_id = platform_get_pcm_device_id(uc_info_rx->id, PCM_CAPTURE);
    ALOGD("rx_device_id = %d", rx_device_id);
    if (rx_device_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)", __func__, uc_info_rx->id);
        retval = -ENODEV;
        goto close_tfa98xx;
    }

    tfa98xx_out = pcm_open(adev->snd_card, rx_device_id, PCM_IN, &pcm_config_tfa98xx);
    if (tfa98xx_out && !pcm_is_ready(tfa98xx_out)) {
        ALOGE("%s: %s", __func__, pcm_get_error(tfa98xx_out));
        retval = -EIO;
        goto close_tfa98xx;
    }

    if (pcm_start(tfa98xx_out) < 0) {
        ALOGE("%s: pcm start for TX failed", __func__);
        retval = -EINVAL;
        goto close_tfa98xx;
    }

    return 0;

close_tfa98xx:
    if (tfa98xx_out) {
        pcm_close(tfa98xx_out);
    }
    tfa98xx_out = NULL;
    disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    list_remove(&uc_info_rx->list);
    uc_info_rx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    uc_info_rx->type = PCM_CAPTURE;
    uc_info_rx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    disable_audio_route(adev, uc_info_rx);
    free(uc_info_rx);
    return retval;
}


void audio_extn_tfa98xx_stop_feedback(struct audio_device *adev, snd_device_t snd_device)
{
    struct audio_usecase *usecase;

    if (!check_snd_device(snd_device)) {
        return;
    }

    usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
    if (tfa98xx_out) {
        pcm_close(tfa98xx_out);
    }
    tfa98xx_out = NULL;
    disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    if (usecase) {
        list_remove(&usecase->list);
        disable_audio_route(adev, usecase);
        free(usecase);
    }
    return;
}
