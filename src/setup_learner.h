#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "config.h"

namespace sampler {

enum class LearnResult { Accepted, Duplicate, Unsupported, Ignored };

constexpr Action kLearnOrder[] = {Action::Next, Action::Prev, Action::Toggle, Action::Jump,
                                  Action::Quit};

class SetupLearner {
public:
    explicit SetupLearner(InputMode mode);

    bool done() const;
    Action current() const;
    Action conflict() const;

    LearnResult offerKey(const std::string& bytes);
    LearnResult offerMidi(const std::string& port, const std::vector<unsigned char>& message);

    Config result() const;

private:
    InputMode mode_;
    size_t step_ = 0;
    Action conflict_ = Action::Next;
    std::string port_;
    std::map<Action, std::string> keys_;
    std::map<Action, MidiBinding> midi_;
};

}  // namespace sampler
