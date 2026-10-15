#include "adapters/rtmidi_input.h"

#include <fcntl.h>
#include <unistd.h>

#include "RtMidi.h"

namespace sampler {
namespace {
constexpr int kReadEnd = 0;
constexpr int kWriteEnd = 1;
}  // namespace

RtMidiInput::RtMidiInput(const Config& config) : config_(config) {}

RtMidiInput::~RtMidiInput() {
    in_.reset();
    for (int end : pipe_) {
        if (end != kClosedFd) ::close(end);
    }
}

bool RtMidiInput::open() {
    if (::pipe(pipe_) != 0) return false;
    ::fcntl(pipe_[kReadEnd], F_SETFL, O_NONBLOCK);
    ::fcntl(pipe_[kWriteEnd], F_SETFL, O_NONBLOCK);
    try {
        in_ = std::make_unique<RtMidiIn>();
        for (unsigned int i = 0; i < in_->getPortCount(); ++i) {
            if (!samePort(config_.midiPort, in_->getPortName(i))) continue;
            in_->setCallback(&RtMidiInput::onMessage, this);
            in_->openPort(i);
            return true;
        }
    } catch (const RtMidiError&) {
    }
    in_.reset();
    return false;
}

int RtMidiInput::fd() const { return pipe_[kReadEnd]; }

bool RtMidiInput::readAction(Action& outAction) {
    char byte;
    if (::read(pipe_[kReadEnd], &byte, sizeof(byte)) <= 0) return false;
    outAction = static_cast<Action>(byte);
    return true;
}

void RtMidiInput::onMessage(double, std::vector<unsigned char>* message, void* userData) {
    auto* self = static_cast<RtMidiInput*>(userData);
    Action action;
    if (!matchMidi(self->config_.midi, *message, action)) return;
    const char byte = static_cast<char>(action);
    // a full pipe drops the press rather than block the MIDI thread
    [[maybe_unused]] const ssize_t written = ::write(self->pipe_[kWriteEnd], &byte, sizeof(byte));
}

}  // namespace sampler
