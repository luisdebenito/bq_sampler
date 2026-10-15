#pragma once
#include "domain/sample.h"
#include "domain/transport.h"
#include "ports/audio_device.h"

struct ma_device;

namespace sampler {

constexpr uint32_t kPeriodFrames = 256;
constexpr const char* kBackendEnvVar = "SAMPLER_AUDIO_BACKEND";

class MiniaudioDevice : public AudioDevice {
public:
    MiniaudioDevice();
    ~MiniaudioDevice() override;

    bool open();
    void bind(Transport* transport, const SampleLibrary* library);

    bool start() override;
    void stop() override;
    uint32_t sampleRate() const override;

private:
    static void callback(ma_device* device, void* output, const void* input,
                         unsigned int frameCount);

    struct Impl;
    Impl* impl_;
    Transport* transport_ = nullptr;
    const SampleLibrary* library_ = nullptr;
    uint32_t sampleRate_ = 0;
    bool opened_ = false;
    bool started_ = false;
};

}  // namespace sampler
