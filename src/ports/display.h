#pragma once

namespace sampler {

class Display {
public:
    virtual ~Display() = default;
    virtual void draw(int index, bool visible) = 0;
};

}  // namespace sampler
