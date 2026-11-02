// Mirrorly - low-latency PCM output (44.1 kHz, 16-bit stereo) via waveOut
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class AudioOut {
public:
    ~AudioOut() { stop(); }
    bool start();
    void stop();
    bool running() const { return running_; }
    void push(const int16_t* interleaved, int frames);
    void flush();
    void setGain(float g) { gain_ = g; }
    float level() const { return level_; }  // recent peak 0..1 (for UI)

private:
    void loop();
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex m_;
    std::vector<int16_t> ring_;
    size_t rd_ = 0, wr_ = 0, count_ = 0;  // in samples (not frames)
    bool primed_ = false;
    std::atomic<float> gain_{1.0f};
    std::atomic<float> level_{0.0f};
};
