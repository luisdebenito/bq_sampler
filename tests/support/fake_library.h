#pragma once
#include <string>
#include <vector>
#include "domain/sample.h"

inline sampler::SampleLibrary makeLib(std::vector<uint64_t> lengths) {
    sampler::SampleLibrary lib;
    lib.sampleRate = 48000;
    for (size_t i = 0; i < lengths.size(); ++i) {
        sampler::Sample s;
        s.frameCount = lengths[i];
        s.name = "s" + std::to_string(i);
        s.frames.resize(lengths[i] * sampler::kChannels);
        for (uint64_t j = 0; j < lengths[i]; ++j) {
            float v = 0.1f * float(i + 1) + float(j) / 1000.0f;
            for (uint32_t c = 0; c < sampler::kChannels; ++c) {
                s.frames[j * sampler::kChannels + c] = v;
            }
        }
        lib.samples.push_back(std::move(s));
    }
    return lib;
}
