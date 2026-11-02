// Mirrorly - low-latency PCM output (16-bit stereo, 44.1 or 48 kHz) via waveOut
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
    // Starts playback at the given sample rate (restarts if it changes).
    bool start(int rate = 44100);
    int rate() const { return rate_; }
    void stop();
    bool running() const { return running_; }
    void push(const int16_t* interleaved, int frames);
    void flush();
    void setGain(float g) { gain_ = g; }
    float level() const { return level_; }  // recent peak 0..1 (for UI)
    // Copies the most recent n mono samples being played (-1..1), oldest first. Returns n.
    int scope(float* out, int n);

private:
    void loop();
    std::atomic<bool> running_{false};
    int rate_ = 44100;
    size_t prime_ = 0, maxQueue_ = 0, trimTo_ = 0;  // in samples
    std::thread thread_;
    std::mutex m_;
    std::vector<int16_t> ring_;
    size_t rd_ = 0, wr_ = 0, count_ = 0;  // in samples (not frames)
    bool primed_ = false;
    std::atomic<float> gain_{1.0f};
    std::atomic<float> level_{0.0f};
    std::mutex scopeM_;
    std::vector<float> scope_ = std::vector<float>(4096, 0.0f);
    size_t scopePos_ = 0;
};
