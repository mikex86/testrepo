/*
 * AC3 decoder
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "avcodec.h"

#include <inttypes.h>
#include "libac3/ac3.h"

/* currently, I use libac3 which is Copyright (C) Aaron Holtzman and
   released under the GPL license. I may reimplement it someday... */
typedef struct AC3DecodeState {
    UINT8 inbuf[4096]; /* input buffer */
    UINT8 *inbuf_ptr;
    int frame_size;
    int flags;
    ac3_state_t state;
} AC3DecodeState;

static int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeState *s = avctx->priv_data;

    ac3_init ();
    s->inbuf_ptr = s->inbuf;
    s->frame_size = 0;
    return 0;
}

stream_samples_t samples;

/**** the following two functions comes from ac3dec */
static inline int blah (int32_t i)
{
    if (i > 0x43c07fff)
	return 32767;
    else if (i < 0x43bf8000)
	return -32768;
    else
	return i - 0x43c00000;
}

static inline void float_to_int (float * _f, INT16 * s16) 
{
    int i;
    int32_t * f = (int32_t *) _f;	// XXX assumes IEEE float format

    for (i = 0; i < 256; i++) {
	s16[2*i] = blah (f[i]);
	s16[2*i+1] = blah (f[i+256]);
    }
}

static inline void float_to_int_mono (float * _f, INT16 * s16) 
{
    int i;
    int32_t * f = (int32_t *) _f;	// XXX assumes IEEE float format

    for (i = 0; i < 256; i++) {
	s16[i] = blah (f[i]);
    }
}

/**** end */

#define HEADER_SIZE 7

static int ac3_decode_frame(AVCodecContext *avctx, 
                            void *data, int *data_size,
                            UINT8 *buf, int buf_size)
{
    AC3DecodeState *s = avctx->priv_data;
    UINT8 *buf_ptr;
    int flags, i, len;
    int sample_rate, bit_rate;
    short *out_samples = data;
    float level;

    *data_size = 0;
    buf_ptr = buf;
    while (buf_size > 0) {
        len = s->inbuf_ptr - s->inbuf;
        if (s->frame_size == 0) {
            /* no header seen : find one. We need at least 7 bytes to parse it */
            len = HEADER_SIZE - len;
            if (len > buf_size)
                len = buf_size;
            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
            if ((s->inbuf_ptr - s->inbuf) == HEADER_SIZE) {
                len = ac3_syncinfo (s->inbuf, &s->flags, &sample_rate, &bit_rate);
                if (len == 0) {
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memcpy(s->inbuf, s->inbuf + 1, HEADER_SIZE - 1);
                    s->inbuf_ptr--;
                } else {
                    s->frame_size = len;
                    /* update codec info */
                    avctx->sample_rate = sample_rate;
                    if ((s->flags & AC3_CHANNEL_MASK) == AC3_MONO)
                        avctx->channels = 1;
                    else
                        avctx->channels = 2;
                    avctx->bit_rate = bit_rate;
                }
            }
        } else if (len < s->frame_size) {
            len = s->frame_size - len;
            if (len > buf_size)
                len = buf_size;
            
            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
        } else {
            if (avctx->channels == 1)
                flags = AC3_MONO;
            else
                flags = AC3_STEREO;

            flags |= AC3_ADJUST_LEVEL;
            level = 1;
            if (ac3_frame (&s->state, s->inbuf, &flags, &level, 384)) {
            fail:
                s->inbuf_ptr = s->inbuf;
                s->frame_size = 0;
                continue;
            }
            for (i = 0; i < 6; i++) {
                if (ac3_block (&s->state))
                    goto fail;
                if (avctx->channels == 1)
                    float_to_int_mono (*samples, out_samples + i * 256);
                else
                    float_to_int (*samples, out_samples + i * 512);
            }
            s->inbuf_ptr = s->inbuf;
            s->frame_size = 0;
            *data_size = 6 * avctx->channels * 256 * sizeof(INT16);
            break;
        }
    }
    return buf_ptr - buf;
}

static int ac3_decode_end(AVCodecContext *s)
{
    return 0;
}

AVCodec ac3_decoder = {
    "ac3",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3DecodeState),
    ac3_decode_init,
    NULL,
    ac3_decode_end,
    ac3_decode_frame,
};
