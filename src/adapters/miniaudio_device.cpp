#include "adapters/miniaudio_device.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>

#include "miniaudio.h"

namespace sampler {

struct MiniaudioDevice::Impl {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
};

namespace {

bool backendFromName(const char* name, ma_backend& out) {
    if (name == nullptr) return false;
    const std::string n(name);
    if (n == "alsa") { out = ma_backend_alsa; return true; }
    if (n == "pulseaudio") { out = ma_backend_pulseaudio; return true; }
    if (n == "jack") { out = ma_backend_jack; return true; }
    if (n == "coreaudio") { out = ma_backend_coreaudio; return true; }
    return false;
}

}  // namespace

MiniaudioDevice::MiniaudioDevice() : impl_(new Impl()) {}

MiniaudioDevice::~MiniaudioDevice() {
    stop();
    if (opened_) ma_device_uninit(&impl_->device);
    if (impl_->contextReady) ma_context_uninit(&impl_->context);
    delete impl_;
}

void MiniaudioDevice::callback(ma_device* device, void* output, const void*,
                               unsigned int frameCount) {
    auto* self = static_cast<MiniaudioDevice*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    if (self->transport_ == nullptr || self->library_ == nullptr) {
        std::fill(out, out + frameCount * kChannels, 0.0f);
        return;
    }
    self->transport_->render(out, frameCount, *self->library_);
}

bool MiniaudioDevice::open() {
    ma_backend forced;
    const bool hasForced = backendFromName(std::getenv(kBackendEnvVar), forced);
    if (ma_context_init(hasForced ? &forced : nullptr, hasForced ? 1 : 0, nullptr,
                        &impl_->context) != MA_SUCCESS) {
        return false;
    }
    impl_->contextReady = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.periodSizeInFrames = kPeriodFrames;
    cfg.dataCallback = &MiniaudioDevice::callback;
    cfg.pUserData = this;

    if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) return false;
    opened_ = true;
    sampleRate_ = impl_->device.sampleRate;
    return true;
}

void MiniaudioDevice::bind(Transport* transport, const SampleLibrary* library) {
    transport_ = transport;
    library_ = library;
}

bool MiniaudioDevice::start() {
    if (!opened_ || started_) return started_;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) return false;
    started_ = true;
    return true;
}

void MiniaudioDevice::stop() {
    if (!started_) return;
    ma_device_stop(&impl_->device);
    started_ = false;
}

uint32_t MiniaudioDevice::sampleRate() const { return sampleRate_; }

}  // namespace sampler
