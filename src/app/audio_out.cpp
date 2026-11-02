#include "audio_out.h"

#include <mmsystem.h>

#include <algorithm>
#include <cmath>

namespace {
const int kRate = 44100;
const int kBufFrames = 441 * 2;   // 20 ms per waveOut buffer
const int kNumBufs = 5;           // ~100 ms queued in the device
const size_t kPrime = 44100 * 2 * 60 / 1000;   // start playback once 60 ms are buffered
const size_t kMaxQueue = 44100 * 2 * 300 / 1000;  // above 300 ms, drop old audio
const size_t kTrimTo = 44100 * 2 * 100 / 1000;
}

bool AudioOut::start() {
    if (running_) return true;
    ring_.assign(kRate * 2 * 2, 0);  // 2 s of stereo samples
    rd_ = wr_ = count_ = 0;
    primed_ = false;
    running_ = true;
    thread_ = std::thread(&AudioOut::loop, this);
    return true;
}

void AudioOut::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void AudioOut::flush() {
    std::lock_guard<std::mutex> lk(m_);
    rd_ = wr_ = count_ = 0;
    primed_ = false;
}

void AudioOut::push(const int16_t* pcm, int frames) {
    if (!running_) return;
    std::lock_guard<std::mutex> lk(m_);
    size_t n = (size_t)frames * 2;
    size_t cap = ring_.size();
    for (size_t i = 0; i < n; i++) {
        ring_[wr_] = pcm[i];
        wr_ = (wr_ + 1) % cap;
        if (count_ < cap) count_++;
        else rd_ = (rd_ + 1) % cap;
    }
    if (count_ > kMaxQueue) {  // sender is ahead of the sound card: catch up
        size_t drop = count_ - kTrimTo;
        drop -= drop % 2;
        rd_ = (rd_ + drop) % cap;
        count_ -= drop;
    }
}

int AudioOut::scope(float* out, int n) {
    std::lock_guard<std::mutex> lk(scopeM_);
    size_t cap = scope_.size();
    if ((size_t)n > cap) n = (int)cap;
    size_t start = (scopePos_ + cap - (size_t)n) % cap;
    for (int i = 0; i < n; i++) out[i] = running_ ? scope_[(start + i) % cap] : 0.0f;
    return n;
}

void AudioOut::loop() {
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = kRate * 4;
    HWAVEOUT wo = nullptr;
    if (waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(ev);
        running_ = false;
        return;
    }
    std::vector<std::vector<int16_t>> bufs(kNumBufs, std::vector<int16_t>(kBufFrames * 2));
    std::vector<WAVEHDR> hdrs(kNumBufs);
    for (int i = 0; i < kNumBufs; i++) {
        hdrs[i] = WAVEHDR{};
        hdrs[i].lpData = (LPSTR)bufs[i].data();
        hdrs[i].dwBufferLength = kBufFrames * 4;
        waveOutPrepareHeader(wo, &hdrs[i], sizeof(WAVEHDR));
        hdrs[i].dwFlags |= WHDR_DONE;
    }
    while (running_) {
        for (int i = 0; i < kNumBufs; i++) {
            if (!(hdrs[i].dwFlags & WHDR_DONE)) continue;
            int16_t* out = bufs[i].data();
            size_t want = (size_t)kBufFrames * 2;
            float g = gain_;
            int peak = 0;
            {
                std::lock_guard<std::mutex> lk(m_);
                if (!primed_ && count_ >= kPrime) primed_ = true;
                size_t avail = primed_ ? std::min(count_, want) : 0;
                size_t cap = ring_.size();
                for (size_t k = 0; k < avail; k++) {
                    int v = (int)lrintf(ring_[rd_] * g);
                    v = std::max(-32768, std::min(32767, v));
                    out[k] = (int16_t)v;
                    peak = std::max(peak, std::abs(v));
                    rd_ = (rd_ + 1) % cap;
                }
                count_ -= avail;
                if (avail < want) {
                    std::fill(out + avail, out + want, 0);
                    if (primed_ && count_ == 0 && avail == 0) primed_ = false;  // underrun: re-prime
                }
            }
            level_ = level_ * 0.7f + 0.3f * (peak / 32768.0f);
            {
                std::lock_guard<std::mutex> lk(scopeM_);
                for (size_t k = 0; k + 1 < want; k += 2) {
                    scope_[scopePos_] = (out[k] + out[k + 1]) / 65536.0f;
                    scopePos_ = (scopePos_ + 1) % scope_.size();
                }
            }
            hdrs[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(wo, &hdrs[i], sizeof(WAVEHDR));
        }
        WaitForSingleObject(ev, 30);
    }
    waveOutReset(wo);
    for (int i = 0; i < kNumBufs; i++) waveOutUnprepareHeader(wo, &hdrs[i], sizeof(WAVEHDR));
    waveOutClose(wo);
    CloseHandle(ev);
}
