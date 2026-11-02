/*
 * Mirrorly native helper library
 *
 *  - FairPlay (playfair) handshake + key decryption used by AirPlay screen mirroring
 *  - H.264 video decoding to BGRA (FFmpeg libavcodec + libswscale)
 *  - ALAC / AAC-LC / AAC-ELD audio decoding to interleaved 16-bit stereo PCM
 *
 * The FairPlay reply tables and playfair code come from the shairplay / RPiPlay /
 * UxPlay projects and are licensed under the GNU GPL v3. This file is GPL v3 too.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>

#include "playfair.h"

#include "native_media.h"
#define EXPORT

/* ------------------------------------------------------------------------- */
/*  FairPlay                                                                 */
/* ------------------------------------------------------------------------- */

static const unsigned char reply_message[4][142] = {
{0x46,0x50,0x4c,0x59,0x03,0x01,0x02,0x00,0x00,0x00,0x00,0x82,0x02,0x00,0x0f,0x9f,0x3f,0x9e,0x0a,0x25,0x21,0xdb,0xdf,0x31,0x2a,0xb2,0xbf,0xb2,0x9e,0x8d,0x23,0x2b,0x63,0x76,0xa8,0xc8,0x18,0x70,0x1d,0x22,0xae,0x93,0xd8,0x27,0x37,0xfe,0xaf,0x9d,0xb4,0xfd,0xf4,0x1c,0x2d,0xba,0x9d,0x1f,0x49,0xca,0xaa,0xbf,0x65,0x91,0xac,0x1f,0x7b,0xc6,0xf7,0xe0,0x66,0x3d,0x21,0xaf,0xe0,0x15,0x65,0x95,0x3e,0xab,0x81,0xf4,0x18,0xce,0xed,0x09,0x5a,0xdb,0x7c,0x3d,0x0e,0x25,0x49,0x09,0xa7,0x98,0x31,0xd4,0x9c,0x39,0x82,0x97,0x34,0x34,0xfa,0xcb,0x42,0xc6,0x3a,0x1c,0xd9,0x11,0xa6,0xfe,0x94,0x1a,0x8a,0x6d,0x4a,0x74,0x3b,0x46,0xc3,0xa7,0x64,0x9e,0x44,0xc7,0x89,0x55,0xe4,0x9d,0x81,0x55,0x00,0x95,0x49,0xc4,0xe2,0xf7,0xa3,0xf6,0xd5,0xba},
{0x46,0x50,0x4c,0x59,0x03,0x01,0x02,0x00,0x00,0x00,0x00,0x82,0x02,0x01,0xcf,0x32,0xa2,0x57,0x14,0xb2,0x52,0x4f,0x8a,0xa0,0xad,0x7a,0xf1,0x64,0xe3,0x7b,0xcf,0x44,0x24,0xe2,0x00,0x04,0x7e,0xfc,0x0a,0xd6,0x7a,0xfc,0xd9,0x5d,0xed,0x1c,0x27,0x30,0xbb,0x59,0x1b,0x96,0x2e,0xd6,0x3a,0x9c,0x4d,0xed,0x88,0xba,0x8f,0xc7,0x8d,0xe6,0x4d,0x91,0xcc,0xfd,0x5c,0x7b,0x56,0xda,0x88,0xe3,0x1f,0x5c,0xce,0xaf,0xc7,0x43,0x19,0x95,0xa0,0x16,0x65,0xa5,0x4e,0x19,0x39,0xd2,0x5b,0x94,0xdb,0x64,0xb9,0xe4,0x5d,0x8d,0x06,0x3e,0x1e,0x6a,0xf0,0x7e,0x96,0x56,0x16,0x2b,0x0e,0xfa,0x40,0x42,0x75,0xea,0x5a,0x44,0xd9,0x59,0x1c,0x72,0x56,0xb9,0xfb,0xe6,0x51,0x38,0x98,0xb8,0x02,0x27,0x72,0x19,0x88,0x57,0x16,0x50,0x94,0x2a,0xd9,0x46,0x68,0x8a},
{0x46,0x50,0x4c,0x59,0x03,0x01,0x02,0x00,0x00,0x00,0x00,0x82,0x02,0x02,0xc1,0x69,0xa3,0x52,0xee,0xed,0x35,0xb1,0x8c,0xdd,0x9c,0x58,0xd6,0x4f,0x16,0xc1,0x51,0x9a,0x89,0xeb,0x53,0x17,0xbd,0x0d,0x43,0x36,0xcd,0x68,0xf6,0x38,0xff,0x9d,0x01,0x6a,0x5b,0x52,0xb7,0xfa,0x92,0x16,0xb2,0xb6,0x54,0x82,0xc7,0x84,0x44,0x11,0x81,0x21,0xa2,0xc7,0xfe,0xd8,0x3d,0xb7,0x11,0x9e,0x91,0x82,0xaa,0xd7,0xd1,0x8c,0x70,0x63,0xe2,0xa4,0x57,0x55,0x59,0x10,0xaf,0x9e,0x0e,0xfc,0x76,0x34,0x7d,0x16,0x40,0x43,0x80,0x7f,0x58,0x1e,0xe4,0xfb,0xe4,0x2c,0xa9,0xde,0xdc,0x1b,0x5e,0xb2,0xa3,0xaa,0x3d,0x2e,0xcd,0x59,0xe7,0xee,0xe7,0x0b,0x36,0x29,0xf2,0x2a,0xfd,0x16,0x1d,0x87,0x73,0x53,0xdd,0xb9,0x9a,0xdc,0x8e,0x07,0x00,0x6e,0x56,0xf8,0x50,0xce},
{0x46,0x50,0x4c,0x59,0x03,0x01,0x02,0x00,0x00,0x00,0x00,0x82,0x02,0x03,0x90,0x01,0xe1,0x72,0x7e,0x0f,0x57,0xf9,0xf5,0x88,0x0d,0xb1,0x04,0xa6,0x25,0x7a,0x23,0xf5,0xcf,0xff,0x1a,0xbb,0xe1,0xe9,0x30,0x45,0x25,0x1a,0xfb,0x97,0xeb,0x9f,0xc0,0x01,0x1e,0xbe,0x0f,0x3a,0x81,0xdf,0x5b,0x69,0x1d,0x76,0xac,0xb2,0xf7,0xa5,0xc7,0x08,0xe3,0xd3,0x28,0xf5,0x6b,0xb3,0x9d,0xbd,0xe5,0xf2,0x9c,0x8a,0x17,0xf4,0x81,0x48,0x7e,0x3a,0xe8,0x63,0xc6,0x78,0x32,0x54,0x22,0xe6,0xf7,0x8e,0x16,0x6d,0x18,0xaa,0x7f,0xd6,0x36,0x25,0x8b,0xce,0x28,0x72,0x6f,0x66,0x1f,0x73,0x88,0x93,0xce,0x44,0x31,0x1e,0x4b,0xe6,0xc0,0x53,0x51,0x93,0xe5,0xef,0x72,0xe8,0x68,0x62,0x33,0x72,0x9c,0x22,0x7d,0x82,0x0c,0x99,0x94,0x45,0xd8,0x92,0x46,0xc8,0xc3,0x59}};

static const unsigned char fp_header[12] = {0x46,0x50,0x4c,0x59,0x03,0x01,0x04,0x00,0x00,0x00,0x00,0x14};

/* Phase 1: 16-byte request -> 142-byte reply. Returns 0 on success. */
EXPORT int mn_fairplay_setup(const uint8_t *req, uint8_t *res)
{
    if (!req || !res || req[4] != 0x03) return -1;
    int mode = req[14];
    if (mode < 0 || mode > 3) return -2;
    memcpy(res, reply_message[mode], 142);
    return 0;
}

/* Phase 2: 164-byte request -> 32-byte reply. Caller keeps the 164-byte request (keymsg). */
EXPORT int mn_fairplay_handshake(const uint8_t *req, uint8_t *res)
{
    if (!req || !res || req[4] != 0x03 || req[12] > 3) return -1;
    memcpy(res, fp_header, 12);
    memcpy(res + 12, req + 144, 20);
    return 0;
}

/* Decrypt the 72-byte "ekey" from SETUP into the 16-byte AES session key. */
EXPORT int mn_fairplay_decrypt(const uint8_t *keymsg164, const uint8_t *ekey72, uint8_t *key16)
{
    if (!keymsg164 || !ekey72 || !key16) return -1;
    if (keymsg164[12] > 3) return -2;  /* invalid FairPlay mode: playfair would read out of bounds */
    unsigned char msg[164], ek[72];
    memcpy(msg, keymsg164, 164);
    memcpy(ek, ekey72, 72);
    playfair_decrypt(msg, ek, key16);
    return 0;
}

/* ------------------------------------------------------------------------- */
/*  Video                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    AVCodecContext *ctx;
    AVPacket *pkt;
    AVFrame *frame;      /* frame being decoded */
    AVFrame *ready;      /* most recent completed frame */
    int has_ready;
    struct SwsContext *sws;
    int sws_w, sws_h, sws_fmt, sws_range, sws_space, sws_dw, sws_dh;
} mn_vdec;

EXPORT const char *mn_version(void)
{
    return "mirrorly-native 1.0 / libavcodec " AV_STRINGIFY(LIBAVCODEC_VERSION);
}

EXPORT void *mn_vdec_create(int threads)
{
    av_log_set_level(AV_LOG_QUIET);
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) return NULL;
    mn_vdec *d = calloc(1, sizeof(mn_vdec));
    if (!d) return NULL;
    d->ctx = avcodec_alloc_context3(codec);
    d->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    d->ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    /* Slice threading only: frame threading delays output by N frames, which would
     * leave a stale picture on screen whenever the iPhone stops sending (static screen). */
    d->ctx->thread_type = FF_THREAD_SLICE;
    d->ctx->thread_count = threads > 0 ? threads : 4;
    if (avcodec_open2(d->ctx, codec, NULL) < 0) {
        avcodec_free_context(&d->ctx);
        free(d);
        return NULL;
    }
    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    d->ready = av_frame_alloc();
    return d;
}

/* Feed Annex-B H.264 data. Returns number of new frames produced (>=0), or <0 on error. */
EXPORT int mn_vdec_decode(void *h, const uint8_t *data, int len)
{
    mn_vdec *d = (mn_vdec *) h;
    if (!d || !data || len <= 0) return -1;
    uint8_t *buf = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) return -1;
    memcpy(buf, data, len);
    memset(buf + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    av_packet_unref(d->pkt);
    if (av_packet_from_data(d->pkt, buf, len) < 0) { av_free(buf); return -1; }
    int ret = avcodec_send_packet(d->ctx, d->pkt);
    av_packet_unref(d->pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return -2;
    int produced = 0;
    for (;;) {
        ret = avcodec_receive_frame(d->ctx, d->frame);
        if (ret < 0) break;
        av_frame_unref(d->ready);
        av_frame_move_ref(d->ready, d->frame);
        d->has_ready = 1;
        produced++;
    }
    return produced;
}

EXPORT int mn_vdec_frame_size(void *h, int *w, int *hgt)
{
    mn_vdec *d = (mn_vdec *) h;
    if (!d || !d->has_ready) return -1;
    *w = d->ready->width;
    *hgt = d->ready->height;
    return 0;
}

static int sws_space_for(int cs)
{
    switch (cs) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_SMPTE240M: return SWS_CS_SMPTE240M;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
    case AVCOL_SPC_FCC: return SWS_CS_FCC;
    default: return SWS_CS_ITU601;
    }
}

/* Convert the most recent frame into a BGRA buffer of size dst_w x dst_h
 * (normally the frame size; may be smaller to downscale). */
/* Required BGRA row stride (bytes) for a frame of width w. Rows are padded to a multiple of
 * 16 pixels: swscale's fast YUV->RGB path converts 8 pixels at a time and silently skips the
 * remaining columns when the width is not a multiple of 8 (e.g. 1014-pixel portrait streams),
 * which left a strip of stale pixels along the right edge. */
EXPORT int mn_vdec_stride_for(int w)
{
    return ((w + 15) & ~15) * 4;
}

/* Convert the most recent frame into a BGRA buffer of size dst_w x dst_h
 * (normally the frame size; may be smaller to downscale). For a 1:1 conversion, pass a
 * dst_stride of at least mn_vdec_stride_for(dst_w): the padding columns are converted too
 * (from the decoder's padded planes) so every visible column is always written. */
EXPORT int mn_vdec_frame_bgra(void *h, uint8_t *dst, int dst_stride, int dst_w, int dst_h)
{
    mn_vdec *d = (mn_vdec *) h;
    if (!d || !d->has_ready || !dst) return -1;
    AVFrame *f = d->ready;
    int fmt = f->format;
    int range = (f->color_range == AVCOL_RANGE_JPEG || fmt == AV_PIX_FMT_YUVJ420P) ? 1 : 0;
    int space = sws_space_for(f->colorspace);
    int src_fmt = fmt;
    if (fmt == AV_PIX_FMT_YUVJ420P) src_fmt = AV_PIX_FMT_YUV420P;

    int sw = f->width, dw = dst_w;
    if (dst_w == f->width && dst_h == f->height) {
        /* 1:1: widen the conversion to a multiple of 16 when both buffers have room */
        int pw = (f->width + 15) & ~15;
        int chroma_ok = (src_fmt == AV_PIX_FMT_YUV420P || src_fmt == AV_PIX_FMT_YUV422P) ? (f->linesize[1] >= pw / 2 && f->linesize[2] >= pw / 2)
                                                                                         : 0;
        if (pw * 4 <= dst_stride && f->linesize[0] >= pw && chroma_ok) sw = dw = pw;
    }
    if (!d->sws || d->sws_w != sw || d->sws_h != f->height || d->sws_fmt != src_fmt ||
        d->sws_range != range || d->sws_space != space || d->sws_dw != dw || d->sws_dh != dst_h) {
        if (d->sws) sws_freeContext(d->sws);
        int flags = (dw == sw && dst_h == f->height) ? SWS_BILINEAR : SWS_BICUBIC;
        d->sws = sws_getContext(sw, f->height, src_fmt, dw, dst_h, AV_PIX_FMT_BGRA, flags, NULL, NULL, NULL);
        if (!d->sws) return -2;
        const int *coefs = sws_getCoefficients(space);
        sws_setColorspaceDetails(d->sws, coefs, range, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
        d->sws_w = sw; d->sws_h = f->height; d->sws_fmt = src_fmt;
        d->sws_range = range; d->sws_space = space; d->sws_dw = dw; d->sws_dh = dst_h;
    }
    uint8_t *dst_data[4] = { dst, NULL, NULL, NULL };
    int dst_lines[4] = { dst_stride, 0, 0, 0 };
    sws_scale(d->sws, (const uint8_t * const *) f->data, f->linesize, 0, f->height, dst_data, dst_lines);
    return 0;
}

/* Diagnostics: one-line description of the current frame's layout. */
EXPORT int mn_vdec_describe(void *h, char *buf, int len)
{
    mn_vdec *d = (mn_vdec *) h;
    if (!d || !d->has_ready || !buf || len <= 0) return -1;
    AVFrame *f = d->ready;
    const char *fmt = av_get_pix_fmt_name((enum AVPixelFormat) f->format);
    snprintf(buf, len, "frame %dx%d coded %dx%d fmt %s linesize %d/%d/%d range %d space %d conv_w %d",
             f->width, f->height, d->ctx->coded_width, d->ctx->coded_height, fmt ? fmt : "?",
             f->linesize[0], f->linesize[1], f->linesize[2], (int) f->color_range, (int) f->colorspace, d->sws_dw);
    return 0;
}

EXPORT void mn_vdec_flush(void *h)
{
    mn_vdec *d = (mn_vdec *) h;
    if (!d) return;
    avcodec_flush_buffers(d->ctx);
    d->has_ready = 0;
}

EXPORT void mn_vdec_destroy(void *h)
{
    mn_vdec *d = (mn_vdec *) h;
    if (!d) return;
    if (d->sws) sws_freeContext(d->sws);
    av_frame_free(&d->frame);
    av_frame_free(&d->ready);
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->ctx);
    free(d);
}

/* ------------------------------------------------------------------------- */
/*  Audio                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    AVCodecContext *ctx;
    AVPacket *pkt;
    AVFrame *frame;
} mn_adec;

/* ct: 2 = ALAC (352 spf), 4 = AAC-LC (1024 spf), 8 = AAC-ELD (480 spf). All 44100 Hz stereo. */
EXPORT void *mn_adec_create(int ct)
{
    av_log_set_level(AV_LOG_QUIET);
    static const uint8_t alac_cookie[36] = {
        0x00,0x00,0x00,0x24, 0x61,0x6c,0x61,0x63, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x01,0x60, 0x00,0x10,0x28,0x0a, 0x0e,0x02,0x00,0xff,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0xac,0x44 };
    static const uint8_t aac_lc_asc[2] = { 0x12, 0x10 };
    static const uint8_t aac_eld_asc[4] = { 0xf8, 0xe8, 0x50, 0x00 };

    enum AVCodecID id;
    const uint8_t *extra; int extra_len;
    switch (ct) {
    case 2: id = AV_CODEC_ID_ALAC; extra = alac_cookie; extra_len = sizeof(alac_cookie); break;
    case 4: id = AV_CODEC_ID_AAC; extra = aac_lc_asc; extra_len = sizeof(aac_lc_asc); break;
    case 8: id = AV_CODEC_ID_AAC; extra = aac_eld_asc; extra_len = sizeof(aac_eld_asc); break;
    default: return NULL;
    }
    const AVCodec *codec = avcodec_find_decoder(id);
    if (!codec) return NULL;
    mn_adec *a = calloc(1, sizeof(mn_adec));
    if (!a) return NULL;
    a->ctx = avcodec_alloc_context3(codec);
    a->ctx->sample_rate = 44100;
    av_channel_layout_default(&a->ctx->ch_layout, 2);
    a->ctx->extradata = av_mallocz(extra_len + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(a->ctx->extradata, extra, extra_len);
    a->ctx->extradata_size = extra_len;
    if (avcodec_open2(a->ctx, codec, NULL) < 0) {
        avcodec_free_context(&a->ctx);
        free(a);
        return NULL;
    }
    a->pkt = av_packet_alloc();
    a->frame = av_frame_alloc();
    return a;
}

static inline int16_t clip16(float v)
{
    int s = (int) (v * 32767.0f);
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return (int16_t) s;
}

static int sample_at(const AVFrame *f, int ch, int i, int16_t *out)
{
    int nch = f->ch_layout.nb_channels;
    if (ch >= nch) ch = nch - 1;
    switch (f->format) {
    case AV_SAMPLE_FMT_FLTP: *out = clip16(((const float *) f->extended_data[ch])[i]); return 0;
    case AV_SAMPLE_FMT_FLT:  *out = clip16(((const float *) f->extended_data[0])[i * nch + ch]); return 0;
    case AV_SAMPLE_FMT_S16P: *out = ((const int16_t *) f->extended_data[ch])[i]; return 0;
    case AV_SAMPLE_FMT_S16:  *out = ((const int16_t *) f->extended_data[0])[i * nch + ch]; return 0;
    case AV_SAMPLE_FMT_S32P: *out = (int16_t) (((const int32_t *) f->extended_data[ch])[i] >> 16); return 0;
    case AV_SAMPLE_FMT_S32:  *out = (int16_t) (((const int32_t *) f->extended_data[0])[i * nch + ch] >> 16); return 0;
    default: return -1;
    }
}

/* Decode one compressed audio packet. Writes interleaved stereo s16 samples into out.
 * Returns number of sample FRAMES written (each frame = 2 int16), or <0 on error. */
EXPORT int mn_adec_decode(void *h, const uint8_t *data, int len, int16_t *out, int out_capacity_frames)
{
    mn_adec *a = (mn_adec *) h;
    if (!a || !data || len <= 0 || !out) return -1;
    uint8_t *buf = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) return -1;
    memcpy(buf, data, len);
    memset(buf + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    av_packet_unref(a->pkt);
    if (av_packet_from_data(a->pkt, buf, len) < 0) { av_free(buf); return -1; }
    int ret = avcodec_send_packet(a->ctx, a->pkt);
    av_packet_unref(a->pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return -2;
    int written = 0;
    for (;;) {
        ret = avcodec_receive_frame(a->ctx, a->frame);
        if (ret < 0) break;
        int n = a->frame->nb_samples;
        if (written + n > out_capacity_frames) n = out_capacity_frames - written;
        for (int i = 0; i < n; i++) {
            int16_t l = 0, r = 0;
            if (sample_at(a->frame, 0, i, &l) < 0) break;
            sample_at(a->frame, 1, i, &r);
            out[(written + i) * 2] = l;
            out[(written + i) * 2 + 1] = r;
        }
        written += n;
        av_frame_unref(a->frame);
    }
    return written;
}

EXPORT void mn_adec_flush(void *h)
{
    mn_adec *a = (mn_adec *) h;
    if (a) avcodec_flush_buffers(a->ctx);
}

EXPORT void mn_adec_destroy(void *h)
{
    mn_adec *a = (mn_adec *) h;
    if (!a) return;
    av_frame_free(&a->frame);
    av_packet_free(&a->pkt);
    avcodec_free_context(&a->ctx);
    free(a);
}
