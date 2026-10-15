#pragma once
#include <string>

#include "ports/display.h"

namespace sampler {

constexpr const char* kEnterScreen = "\x1b[?1049h";
constexpr const char* kLeaveScreen = "\x1b[?1049l";
constexpr const char* kHideCursor = "\x1b[?25l";
constexpr const char* kShowCursor = "\x1b[?25h";
constexpr const char* kClearScreen = "\x1b[2J";

struct Size {
    int cols;
    int rows;
};

Size terminalSize();
std::string frameFor(int index, bool visible, Size size);

class AnsiDisplay : public Display {
public:
    ~AnsiDisplay() override;

    void enter();
    void leave();
    void draw(int index, bool visible) override;

private:
    bool entered_ = false;
};

}  // namespace sampler
