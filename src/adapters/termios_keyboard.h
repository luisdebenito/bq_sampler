#pragma once
#include <termios.h>

#include <cstddef>
#include <string>

#include "adapters/fd_input.h"
#include "config.h"

namespace sampler {

bool matchAction(const Config& config, const char* buf, size_t len, Action& outAction);

class TermiosKeyboard : public FdInput {
public:
    explicit TermiosKeyboard(const Config& config);
    ~TermiosKeyboard() override;

    bool enterRawMode();
    void restore();

    int fd() const override;
    bool readAction(Action& outAction) override;
    bool readBytes(std::string& out);

private:
    const Config& config_;
    termios original_{};
    bool raw_ = false;
};

}  // namespace sampler
