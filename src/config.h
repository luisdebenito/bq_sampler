#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "domain/action.h"
#include "midi_binding.h"

namespace sampler {

constexpr uint32_t kDefaultRampMs = 2;
constexpr uint32_t kDefaultBlinkMs = 250;

enum class InputMode { Keyboard, Midi };

struct KeyBinding {
    std::string name;
    std::string bytes;
    std::string altBytes;
};

struct Config {
    InputMode input = InputMode::Keyboard;
    std::string midiPort;
    std::map<Action, KeyBinding> keys;
    std::map<Action, MidiBinding> midi;
    uint32_t rampMs = kDefaultRampMs;
    uint32_t blinkMs = kDefaultBlinkMs;
};

bool keyNameToBytes(const std::string& name, KeyBinding& out);
bool keyNameFromBytes(const std::string& bytes, std::string& outName);
Config defaultConfig();
Config loadConfig(const std::string& path);
bool saveConfig(const std::string& path, const Config& config);

}  // namespace sampler
