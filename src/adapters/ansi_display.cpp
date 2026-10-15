#include "adapters/ansi_display.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

namespace sampler {
namespace {
constexpr int kFallbackCols = 80;
constexpr int kFallbackRows = 24;
constexpr int kGlyphRows = 5;
constexpr int kGlyphCols = 3;
constexpr int kGlyphGap = 1;
// a terminal cell is about twice as tall as it is wide
constexpr int kCellAspect = 2;
constexpr char kPixelOn = '#';
constexpr const char* kBlock = "█";
constexpr const char* kGap = " ";

constexpr const char* kGlyphs[][kGlyphRows] = {
    {"###", "#.#", "#.#", "#.#", "###"},
    {".#.", "##.", ".#.", ".#.", "###"},
    {"###", "..#", "###", "#..", "###"},
    {"###", "..#", "###", "..#", "###"},
    {"#.#", "#.#", "###", "..#", "..#"},
    {"###", "#..", "###", "..#", "###"},
    {"###", "#..", "###", "#.#", "###"},
    {"###", "..#", "..#", "..#", "..#"},
    {"###", "#.#", "###", "#.#", "###"},
    {"###", "#.#", "###", "..#", "###"},
};

void writeOut(const std::string& s) {
    ::fwrite(s.data(), 1, s.size(), stdout);
    ::fflush(stdout);
}

std::string moveTo(int row, int col) {
    return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H";
}

std::string repeat(const char* cell, int times) {
    std::string s;
    for (int i = 0; i < times; ++i) s += cell;
    return s;
}

int glyphWidth(int digits) { return (digits * kGlyphCols + (digits - 1) * kGlyphGap) * kCellAspect; }

std::string bigFrame(const std::string& text, bool visible, Size size, int scale) {
    std::string frame = kClearScreen;
    if (!visible) return frame;

    const int pixel = kCellAspect * scale;
    const int top = (size.rows - kGlyphRows * scale) / 2 + 1;
    const int left = (size.cols - glyphWidth(static_cast<int>(text.size())) * scale) / 2 + 1;
    for (int row = 0; row < kGlyphRows * scale; ++row) {
        frame += moveTo(top + row, left);
        for (size_t d = 0; d < text.size(); ++d) {
            if (d > 0) frame += repeat(kGap, kGlyphGap * pixel);
            const char* bits = kGlyphs[text[d] - '0'][row / scale];
            for (int col = 0; col < kGlyphCols; ++col) {
                frame += repeat(bits[col] == kPixelOn ? kBlock : kGap, pixel);
            }
        }
    }
    return frame;
}
}  // namespace

Size terminalSize() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return Size{ws.ws_col, ws.ws_row};
    }
    return Size{kFallbackCols, kFallbackRows};
}

std::string frameFor(int index, bool visible, Size size) {
    const std::string text = std::to_string(index);
    const int scale = std::min(size.cols / glyphWidth(static_cast<int>(text.size())),
                               size.rows / kGlyphRows);
    if (scale >= 1) return bigFrame(text, visible, size, scale);

    const std::string body = visible ? text : std::string(text.size(), ' ');

    const int col = std::max(1, (size.cols - static_cast<int>(text.size())) / 2 + 1);
    const int row = std::max(1, size.rows / 2 + 1);

    return std::string(kClearScreen) + moveTo(row, col) + body;
}

AnsiDisplay::~AnsiDisplay() { leave(); }

void AnsiDisplay::enter() {
    if (entered_) return;
    writeOut(std::string(kEnterScreen) + kHideCursor + kClearScreen);
    entered_ = true;
}

void AnsiDisplay::leave() {
    if (!entered_) return;
    writeOut(std::string(kShowCursor) + kLeaveScreen);
    entered_ = false;
}

void AnsiDisplay::draw(int index, bool visible) {
    writeOut(frameFor(index, visible, terminalSize()));
}

}  // namespace sampler
