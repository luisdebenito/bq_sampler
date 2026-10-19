#pragma once
#include <atomic>
#include <cstdint>
#include "domain/sample.h"

namespace sampler {

class Transport {
public:
    Transport(int sampleCount, uint32_t rampFrames);

    void step(int delta);
    void toggle();
    void jump();
    bool isPlaying() const;
    int armed() const;

    void render(float* out, uint32_t frames, const SampleLibrary& lib);

private:
    void beginFade(float target);
    void advanceGain();

    static constexpr uint64_t kNoCut = UINT64_MAX;

    std::atomic<int> armedIndex_{0};
    std::atomic<bool> playRequested_{false};
    std::atomic<bool> jumpRequested_{false};
    const int sampleCount_;
    const uint32_t rampFrames_;

    int cur_ = 0;
    uint64_t pos_ = 0;
    uint64_t cutAt_ = kNoCut;
    bool prevPlay_ = false;
    float gain_ = 0.0f;
    float gainTarget_ = 0.0f;
    float gainStep_ = 0.0f;
};

}  // namespace sampler
