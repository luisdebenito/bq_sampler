#include <string>

#include "adapters/ansi_display.h"
#include "doctest.h"

using sampler::frameFor;
using sampler::Size;

static std::string blocks(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += "█";
    return s;
}

TEST_CASE("the number fills the window with block digits") {
    auto s = frameFor(7, true, Size{80, 24});
    CHECK(s.find("\x1b[3;29H" + blocks(24)) != std::string::npos);
    CHECK(s.find("\x1b[22;29H" + std::string(16, ' ') + blocks(8)) != std::string::npos);
    CHECK(s.find("\x1b[23;") == std::string::npos);
}

TEST_CASE("the digits grow with the window") {
    auto s = frameFor(7, true, Size{160, 48});
    CHECK(s.find("\x1b[2;54H" + blocks(54)) != std::string::npos);
}

TEST_CASE("two digits sit side by side with a gap") {
    auto s = frameFor(12, true, Size{80, 24});
    const std::string firstRow = std::string(8, ' ') + blocks(8) + std::string(16, ' ') + blocks(24);
    CHECK(s.find("\x1b[3;13H" + firstRow) != std::string::npos);
}

TEST_CASE("a hidden big number leaves the screen blank") {
    CHECK(frameFor(7, false, Size{80, 24}) == sampler::kClearScreen);
}

TEST_CASE("when the big digits do not fit the plain number is centred") {
    auto s = frameFor(7, true, Size{21, 4});
    CHECK(s.find("\x1b[3;11H7") != std::string::npos);
}

TEST_CASE("a hidden number is drawn as blanks of the same width") {
    auto visible = frameFor(42, true, Size{21, 4});
    auto hidden = frameFor(42, false, Size{21, 4});
    CHECK(visible.find("42") != std::string::npos);
    CHECK(hidden.find("42") == std::string::npos);
    CHECK(hidden.find("\x1b[3;10H  ") != std::string::npos);
}

TEST_CASE("the frame clears the screen before drawing") {
    auto s = frameFor(0, true, Size{21, 5});
    CHECK(s.rfind(sampler::kClearScreen, 0) == 0);
}

// Review Focus 5: una terminal mas estrecha que el texto no puede dar columna negativa
TEST_CASE("a terminal narrower than the text clamps to column 1") {
    auto s = frameFor(12345, true, Size{3, 1});
    CHECK(s.find("\x1b[1;1H") != std::string::npos);
    CHECK(s.find(";0H") == std::string::npos);
    CHECK(s.find(";-") == std::string::npos);
}

TEST_CASE("a degenerate terminal size does not crash") {
    auto s = frameFor(1, true, Size{0, 0});
    CHECK(s.find("\x1b[1;1H") != std::string::npos);
}
