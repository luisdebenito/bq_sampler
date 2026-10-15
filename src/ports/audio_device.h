#pragma once
#include <cstdint>

namespace sampler {

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual uint32_t sampleRate() const = 0;
};

}  // namespace sampler
