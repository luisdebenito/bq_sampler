#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sampler {

constexpr uint32_t kChannels = 2;

struct Sample {
    std::vector<float> frames;
    uint64_t frameCount = 0;
    std::string name;
};

struct SampleLibrary {
    std::vector<Sample> samples;
    uint32_t sampleRate = 0;
};

}  // namespace sampler
