#pragma once
#include <poll.h>

#include <vector>

#include "adapters/fd_input.h"
#include "ports/input_source.h"

namespace sampler {

class InputMux : public InputSource {
public:
    explicit InputMux(const std::vector<FdInput*>& inputs);

    bool poll(int timeoutMs, Action& outAction) override;

private:
    std::vector<FdInput*> inputs_;
    std::vector<pollfd> fds_;
};

}  // namespace sampler
