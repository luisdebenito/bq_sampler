#include "adapters/input_mux.h"

namespace sampler {

InputMux::InputMux(const std::vector<FdInput*>& inputs) : inputs_(inputs) {
    for (FdInput* input : inputs_) fds_.push_back(pollfd{input->fd(), POLLIN, 0});
}

bool InputMux::poll(int timeoutMs, Action& outAction) {
    if (::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeoutMs) <= 0) return false;
    for (size_t i = 0; i < fds_.size(); ++i) {
        if ((fds_[i].revents & POLLIN) && inputs_[i]->readAction(outAction)) return true;
    }
    return false;
}

}  // namespace sampler
