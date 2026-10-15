#include "config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sampler {
namespace {

constexpr const char* kInputKey = "input";
constexpr const char* kKeyboardValue = "keyboard";
constexpr const char* kMidiValue = "midi";
constexpr const char* kMidiPortKey = "midi_port";
constexpr const char* kMidiKeyPrefix = "midi_";
constexpr const char* kRampKey = "ramp_ms";
constexpr const char* kBlinkKey = "blink_ms";
constexpr const char* kNoteName = "note";
constexpr const char* kCcName = "cc";
constexpr const char* kAssign = " = ";
constexpr char kFieldSeparator = ':';
constexpr char kLineEnd = '\n';
constexpr size_t kSingleKeyBytes = 1;
constexpr const char* kNamedKeys[] = {"left", "right", "up", "down", "space", "enter", "esc"};

struct ActionName {
    Action action;
    const char* name;
};

constexpr ActionName kActionNames[] = {
    {Action::Prev, "prev"},
    {Action::Next, "next"},
    {Action::Toggle, "toggle"},
    {Action::Quit, "quit"},
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

constexpr size_t kMaxDigits = 9;

bool parseUint(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > kMaxDigits ||
        s.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    out = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10));
    return true;
}

bool actionFromName(const std::string& name, Action& out) {
    for (const auto& entry : kActionNames) {
        if (name == entry.name) {
            out = entry.action;
            return true;
        }
    }
    return false;
}

const char* actionName(Action action) {
    for (const auto& entry : kActionNames) {
        if (entry.action == action) return entry.name;
    }
    return "";
}

bool parseMidiBinding(const std::string& text, MidiBinding& out) {
    const size_t first = text.find(kFieldSeparator);
    if (first == std::string::npos) return false;
    const size_t second = text.find(kFieldSeparator, first + 1);
    if (second == std::string::npos) return false;

    uint32_t channel = 0;
    uint32_t number = 0;
    if (!parseUint(text.substr(first + 1, second - first - 1), channel) ||
        !parseUint(text.substr(second + 1), number)) {
        return false;
    }
    if (channel < kMinMidiChannel || channel > kMaxMidiChannel || number > kMaxMidiDataValue) {
        return false;
    }

    const std::string kind = text.substr(0, first);
    if (kind == kNoteName) {
        out.kind = MidiKind::Note;
    } else if (kind == kCcName) {
        out.kind = MidiKind::Cc;
    } else {
        return false;
    }
    out.channel = static_cast<uint8_t>(channel);
    out.number = static_cast<uint8_t>(number);
    return true;
}

std::string formatMidiBinding(const MidiBinding& b) {
    return std::string(b.kind == MidiKind::Note ? kNoteName : kCcName) + kFieldSeparator +
           std::to_string(b.channel) + kFieldSeparator + std::to_string(b.number);
}

}  // namespace

bool keyNameToBytes(const std::string& name, KeyBinding& out) {
    out.name = name;
    out.altBytes.clear();
    if (name == "left")  { out.bytes = "\x1b[D"; out.altBytes = "\x1bOD"; return true; }
    if (name == "right") { out.bytes = "\x1b[C"; out.altBytes = "\x1bOC"; return true; }
    if (name == "up")    { out.bytes = "\x1b[A"; out.altBytes = "\x1bOA"; return true; }
    if (name == "down")  { out.bytes = "\x1b[B"; out.altBytes = "\x1bOB"; return true; }
    if (name == "space") { out.bytes = " ";      return true; }
    if (name == "enter") { out.bytes = "\r";     out.altBytes = "\n";   return true; }
    if (name == "esc")   { out.bytes = "\x1b";   return true; }
    if (name.size() == 1 && std::isprint(static_cast<unsigned char>(name[0]))) {
        out.bytes = name;
        return true;
    }
    return false;
}

bool keyNameFromBytes(const std::string& bytes, std::string& outName) {
    KeyBinding b;
    for (const char* name : kNamedKeys) {
        keyNameToBytes(name, b);
        if (bytes == b.bytes || (!b.altBytes.empty() && bytes == b.altBytes)) {
            outName = name;
            return true;
        }
    }
    if (bytes.size() != kSingleKeyBytes || !keyNameToBytes(bytes, b)) return false;
    outName = bytes;
    return true;
}

Config defaultConfig() {
    Config c;
    KeyBinding b;
    keyNameToBytes("left", b);   c.keys[Action::Prev] = b;
    keyNameToBytes("right", b);  c.keys[Action::Next] = b;
    keyNameToBytes("space", b);  c.keys[Action::Toggle] = b;
    keyNameToBytes("q", b);      c.keys[Action::Quit] = b;
    return c;
}

Config loadConfig(const std::string& path) {
    Config c = defaultConfig();
    std::ifstream f(path);
    if (!f) return c;

    const std::string midiPrefix = kMidiKeyPrefix;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        if (key == kRampKey) { parseUint(value, c.rampMs); continue; }
        if (key == kBlinkKey) { parseUint(value, c.blinkMs); continue; }
        if (key == kInputKey) {
            if (value == kMidiValue) c.input = InputMode::Midi;
            if (value == kKeyboardValue) c.input = InputMode::Keyboard;
            continue;
        }
        if (key == kMidiPortKey) { c.midiPort = value; continue; }

        Action action;
        if (key.compare(0, midiPrefix.size(), midiPrefix) == 0) {
            MidiBinding binding;
            if (actionFromName(key.substr(midiPrefix.size()), action) &&
                parseMidiBinding(value, binding)) {
                c.midi[action] = binding;
            }
            continue;
        }

        KeyBinding binding;
        if (actionFromName(key, action) && keyNameToBytes(value, binding)) {
            c.keys[action] = binding;
        }
    }
    return c;
}

bool saveConfig(const std::string& path, const Config& config) {
    std::ofstream f(path);
    if (!f) return false;
    if (config.input == InputMode::Midi) {
        f << kInputKey << kAssign << kMidiValue << kLineEnd;
        f << kMidiPortKey << kAssign << config.midiPort << kLineEnd;
        for (const auto& [action, binding] : config.midi) {
            f << kMidiKeyPrefix << actionName(action) << kAssign << formatMidiBinding(binding)
              << kLineEnd;
        }
    } else {
        f << kInputKey << kAssign << kKeyboardValue << kLineEnd;
        for (const auto& [action, binding] : config.keys) {
            f << actionName(action) << kAssign << binding.name << kLineEnd;
        }
    }
    f << kRampKey << kAssign << config.rampMs << kLineEnd;
    f << kBlinkKey << kAssign << config.blinkMs << kLineEnd;
    f.close();
    return !f.fail();
}

}  // namespace sampler
