#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "domain/action.h"

namespace sampler {

constexpr uint8_t kMinMidiChannel = 1;
constexpr uint8_t kMaxMidiChannel = 16;
constexpr uint8_t kMaxMidiDataValue = 127;

enum class MidiKind { Note, Cc };

struct MidiBinding {
    MidiKind kind = MidiKind::Note;
    uint8_t channel = kMinMidiChannel;
    uint8_t number = 0;
};

bool operator==(const MidiBinding& a, const MidiBinding& b);

bool midiBindingFromMessage(const std::vector<unsigned char>& message, MidiBinding& out);
bool matchMidi(const std::map<Action, MidiBinding>& bindings,
               const std::vector<unsigned char>& message, Action& outAction);
std::string stripPortSuffix(const std::string& name);
bool samePort(const std::string& saved, const std::string& actual);

}  // namespace sampler
