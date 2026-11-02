/* Mirrorly - FairPlay + FFmpeg based media helpers (C API). GPL v3. */
#ifndef MIRRORLY_NATIVE_MEDIA_H
#define MIRRORLY_NATIVE_MEDIA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int mn_fairplay_setup(const uint8_t *req16, uint8_t *res142);
int mn_fairplay_handshake(const uint8_t *req164, uint8_t *res32);
int mn_fairplay_decrypt(const uint8_t *keymsg164, const uint8_t *ekey72, uint8_t *key16);

const char *mn_version(void);

void *mn_vdec_create(int threads);
int mn_vdec_decode(void *h, const uint8_t *data, int len);
int mn_vdec_frame_size(void *h, int *w, int *hgt);
int mn_vdec_frame_bgra(void *h, uint8_t *dst, int dst_stride, int dst_w, int dst_h);
void mn_vdec_flush(void *h);
void mn_vdec_destroy(void *h);

void *mn_adec_create(int ct);
int mn_adec_decode(void *h, const uint8_t *data, int len, int16_t *out, int out_capacity_frames);
void mn_adec_flush(void *h);
void mn_adec_destroy(void *h);

#ifdef __cplusplus
}
#endif
#endif
