// Regression test: every visible pixel of every decoded frame must be written by the
// YUV -> BGRA conversion, whatever the frame width (iOS sends e.g. 1014-pixel-wide
// portrait streams; a width that isn't a multiple of 8 used to leave stale columns
// along the right edge).
//
//   ffmpeg -f lavfi -i testsrc2=size=1014x1440:rate=30 -frames:v 10 -c:v libx264 -bf 0 -f h264 odd.h264
//   ./out/linux/video_edges odd.h264 [more.h264 ...]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "native_media.h"

static int checkFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.empty()) {
        printf("  FAIL  cannot read %s\n", path);
        return 1;
    }
    void* dec = mn_vdec_create(4);
    int frames = 0, bad = 0, w = 0, h = 0;
    size_t start = 0;
    // feed one access unit at a time (everything up to and including each slice NAL)
    for (size_t i = 0; i + 3 < d.size(); i++) {
        if (!(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)) continue;
        int t = d[i + 3] & 0x1f;
        if (t != 1 && t != 5) continue;
        size_t k = i + 3;
        while (k + 3 < d.size() && !(d[k] == 0 && d[k + 1] == 0 && d[k + 2] == 1)) k++;
        if (k + 3 >= d.size()) k = d.size();
        else if (d[k - 1] == 0) k--;
        if (mn_vdec_decode(dec, d.data() + start, (int)(k - start)) > 0 && mn_vdec_frame_size(dec, &w, &h) == 0) {
            int stride = mn_vdec_stride_for(w);
            std::vector<uint8_t> px((size_t)stride * h, 0xAB);  // marker
            mn_vdec_frame_bgra(dec, px.data(), stride, w, h);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    const uint8_t* p = &px[(size_t)y * stride + x * 4];
                    if (p[0] == 0xAB && p[1] == 0xAB && p[2] == 0xAB && p[3] == 0xAB) bad++;
                }
            frames++;
        }
        start = k;
        i = k - 1;
    }
    mn_vdec_destroy(dec);
    bool ok = frames > 0 && bad == 0;
    printf("  %s  %s: %d frames at %dx%d, %d pixels left unwritten\n", ok ? "PASS" : "FAIL", path, frames, w, h, bad);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: video_edges file.h264 [...]\n");
        return 2;
    }
    int failures = 0;
    for (int i = 1; i < argc; i++) failures += checkFile(argv[i]);
    printf("\n%s\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
