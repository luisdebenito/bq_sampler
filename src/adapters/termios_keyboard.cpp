#include "adapters/termios_keyboard.h"

#include <unistd.h>

#include <cstring>

namespace sampler {
namespace {
constexpr size_t kReadBufferBytes = 32;
}

bool matchAction(const Config& config, const char* buf, size_t len, Action& outAction) {
    for (const auto& [action, binding] : config.keys) {
        if (!binding.bytes.empty() && binding.bytes.size() == len &&
            std::memcmp(buf, binding.bytes.data(), len) == 0) {
            outAction = action;
            return true;
        }
        if (!binding.altBytes.empty() && binding.altBytes.size() == len &&
            std::memcmp(buf, binding.altBytes.data(), len) == 0) {
            outAction = action;
            return true;
        }
    }
    return false;
}

TermiosKeyboard::TermiosKeyboard(const Config& config) : config_(config) {}

TermiosKeyboard::~TermiosKeyboard() { restore(); }

bool TermiosKeyboard::enterRawMode() {
    if (raw_) return true;
    if (tcgetattr(STDIN_FILENO, &original_) != 0) return false;
    termios raw = original_;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    raw_ = true;
    return true;
}

void TermiosKeyboard::restore() {
    if (!raw_) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    raw_ = false;
}

int TermiosKeyboard::fd() const { return STDIN_FILENO; }

bool TermiosKeyboard::readBytes(std::string& out) {
    char buf[kReadBufferBytes];
    const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return false;
    out.assign(buf, static_cast<size_t>(n));
    return true;
}

bool TermiosKeyboard::readAction(Action& outAction) {
    std::string bytes;
    return readBytes(bytes) && matchAction(config_, bytes.data(), bytes.size(), outAction);
}

}  // namespace sampler
