#include "midi_binding.h"

#include <cctype>

namespace sampler {
namespace {

constexpr unsigned char kStatusTypeMask = 0xF0;
constexpr unsigned char kChannelMask = 0x0F;
constexpr unsigned char kNoteOnStatus = 0x90;
constexpr unsigned char kControlChangeStatus = 0xB0;
constexpr unsigned char kReleaseValue = 0;
constexpr size_t kChannelMessageBytes = 3;
constexpr size_t kStatusByte = 0;
constexpr size_t kNumberByte = 1;
constexpr size_t kValueByte = 2;
constexpr char kAlsaClientPortSeparator = ':';
constexpr char kAlsaSuffixSeparator = ' ';

bool allDigits(const std::string& s, size_t from, size_t to) {
    if (from >= to) return false;
    for (size_t i = from; i < to; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

}  // namespace

bool operator==(const MidiBinding& a, const MidiBinding& b) {
    return a.kind == b.kind && a.channel == b.channel && a.number == b.number;
}

bool midiBindingFromMessage(const std::vector<unsigned char>& message, MidiBinding& out) {
    if (message.size() != kChannelMessageBytes || message[kValueByte] == kReleaseValue) {
        return false;
    }
    const unsigned char type = message[kStatusByte] & kStatusTypeMask;
    if (type == kNoteOnStatus) {
        out.kind = MidiKind::Note;
    } else if (type == kControlChangeStatus) {
        out.kind = MidiKind::Cc;
    } else {
        return false;
    }
    out.channel = static_cast<uint8_t>((message[kStatusByte] & kChannelMask) + kMinMidiChannel);
    out.number = message[kNumberByte];
    return true;
}

bool matchMidi(const std::map<Action, MidiBinding>& bindings,
               const std::vector<unsigned char>& message, Action& outAction) {
    MidiBinding pressed;
    if (!midiBindingFromMessage(message, pressed)) return false;
    for (const auto& [action, binding] : bindings) {
        if (binding == pressed) {
            outAction = action;
            return true;
        }
    }
    return false;
}

std::string stripPortSuffix(const std::string& name) {
    const size_t colon = name.rfind(kAlsaClientPortSeparator);
    if (colon == std::string::npos) return name;
    const size_t space = name.rfind(kAlsaSuffixSeparator, colon);
    if (space == std::string::npos) return name;
    if (!allDigits(name, space + 1, colon) || !allDigits(name, colon + 1, name.size())) {
        return name;
    }
    return name.substr(0, space);
}

bool samePort(const std::string& saved, const std::string& actual) {
    return !saved.empty() && stripPortSuffix(saved) == stripPortSuffix(actual);
}

}  // namespace sampler
