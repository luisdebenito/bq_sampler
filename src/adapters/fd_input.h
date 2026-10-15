#pragma once
#include "domain/action.h"

namespace sampler {

class FdInput {
public:
    virtual ~FdInput() = default;
    virtual int fd() const = 0;
    virtual bool readAction(Action& outAction) = 0;
};

}  // namespace sampler
