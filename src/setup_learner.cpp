#include "setup_learner.h"

#include <iterator>

namespace sampler {
namespace {

template <typename T>
bool taken(const std::map<Action, T>& learned, const T& value, Action& owner) {
    for (const auto& [action, known] : learned) {
        if (known == value) {
            owner = action;
            return true;
        }
    }
    return false;
}

}  // namespace

SetupLearner::SetupLearner(InputMode mode) : mode_(mode) {}

bool SetupLearner::done() const { return step_ == std::size(kLearnOrder); }

Action SetupLearner::current() const { return kLearnOrder[step_]; }

Action SetupLearner::conflict() const { return conflict_; }

LearnResult SetupLearner::offerKey(const std::string& bytes) {
    std::string name;
    if (!keyNameFromBytes(bytes, name)) return LearnResult::Unsupported;
    if (taken(keys_, name, conflict_)) return LearnResult::Duplicate;
    keys_[current()] = name;
    ++step_;
    return LearnResult::Accepted;
}

LearnResult SetupLearner::offerMidi(const std::string& port,
                                    const std::vector<unsigned char>& message) {
    MidiBinding binding;
    if (!midiBindingFromMessage(message, binding)) return LearnResult::Ignored;
    if (!port_.empty() && port != port_) return LearnResult::Ignored;
    if (taken(midi_, binding, conflict_)) return LearnResult::Duplicate;
    port_ = port;
    midi_[current()] = binding;
    ++step_;
    return LearnResult::Accepted;
}

Config SetupLearner::result() const {
    Config config = defaultConfig();
    config.input = mode_;
    config.midiPort = stripPortSuffix(port_);
    config.midi = midi_;
    for (const auto& [action, name] : keys_) keyNameToBytes(name, config.keys[action]);
    return config;
}

}  // namespace sampler
