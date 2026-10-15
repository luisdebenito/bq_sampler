#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "adapters/fd_input.h"
#include "config.h"

class RtMidiIn;

namespace sampler {

constexpr int kClosedFd = -1;
constexpr size_t kPipeEnds = 2;

class RtMidiInput : public FdInput {
public:
    explicit RtMidiInput(const Config& config);
    ~RtMidiInput() override;

    bool open();

    int fd() const override;
    bool readAction(Action& outAction) override;

private:
    static void onMessage(double timeStamp, std::vector<unsigned char>* message, void* userData);

    const Config& config_;
    std::unique_ptr<RtMidiIn> in_;
    int pipe_[kPipeEnds] = {kClosedFd, kClosedFd};
};

}  // namespace sampler
