#include "domain/transport.h"

#include <algorithm>

namespace sampler {

Transport::Transport(int sampleCount, uint32_t rampFrames)
    : sampleCount_(sampleCount), rampFrames_(rampFrames) {}

void Transport::step(int delta) {
    if (sampleCount_ <= 0) return;
    int next = armedIndex_.load(std::memory_order_relaxed) + delta;
    next = ((next % sampleCount_) + sampleCount_) % sampleCount_;
    armedIndex_.store(next, std::memory_order_release);
}

void Transport::toggle() {
    playRequested_.store(!playRequested_.load(std::memory_order_relaxed),
                         std::memory_order_release);
}

bool Transport::isPlaying() const {
    return playRequested_.load(std::memory_order_relaxed);
}

int Transport::armed() const {
    return armedIndex_.load(std::memory_order_relaxed);
}

void Transport::beginFade(float target) {
    gainTarget_ = target;
    if (rampFrames_ == 0) {
        gain_ = target;
        gainStep_ = 0.0f;
        return;
    }
    gainStep_ = (target - gain_) / static_cast<float>(rampFrames_);
}

void Transport::advanceGain() {
    if (gain_ == gainTarget_) return;
    gain_ += gainStep_;
    gain_ = std::min(std::max(gain_, 0.0f), 1.0f);
    if ((gainStep_ > 0.0f && gain_ >= gainTarget_) ||
        (gainStep_ < 0.0f && gain_ <= gainTarget_)) {
        gain_ = gainTarget_;
    }
}

void Transport::render(float* out, uint32_t frames, const SampleLibrary& lib) {
    const uint32_t outSamples = frames * kChannels;
    const bool play = playRequested_.load(std::memory_order_acquire);

    if (play && !prevPlay_) {
        cur_ = armedIndex_.load(std::memory_order_acquire);
        pos_ = 0;
        beginFade(1.0f);
    } else if (!play && prevPlay_) {
        beginFade(0.0f);
    }
    prevPlay_ = play;

    const bool haveLibrary = sampleCount_ > 0 &&
                             static_cast<size_t>(sampleCount_) <= lib.samples.size();
    const bool silent = !haveLibrary || (!play && gain_ <= 0.0f);
    if (silent) {
        pos_ = 0;
        if (!play) gain_ = 0.0f;
        std::fill(out, out + outSamples, 0.0f);
        return;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        const Sample& cs = lib.samples[static_cast<size_t>(cur_)];
        if (pos_ >= cs.frameCount) {
            const int next = armedIndex_.load(std::memory_order_acquire);
            if (next != cur_) {
                cur_ = next;
                beginFade(1.0f);
            }
            pos_ = 0;
        }

        const Sample& s = lib.samples[static_cast<size_t>(cur_)];
        if (play && rampFrames_ > 0) {
            const uint64_t fadeOutStart =
                s.frameCount > rampFrames_ ? s.frameCount - rampFrames_ : 0;
            const bool switching =
                armedIndex_.load(std::memory_order_acquire) != cur_;
            const float target = (switching && pos_ >= fadeOutStart) ? 0.0f : 1.0f;
            if (target != gainTarget_) beginFade(target);
        }

        advanceGain();
        const float* src = &s.frames[pos_ * kChannels];
        for (uint32_t c = 0; c < kChannels; ++c) {
            out[i * kChannels + c] = src[c] * gain_;
        }
        ++pos_;
    }

    if (!play && gain_ <= 0.0f) pos_ = 0;
}

}  // namespace sampler
