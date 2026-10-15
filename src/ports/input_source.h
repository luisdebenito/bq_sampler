#pragma once
#include "domain/action.h"

namespace sampler {

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual bool poll(int timeoutMs, Action& outAction) = 0;
};

}  // namespace sampler
