#include "doctest.h"
#include "setup_learner.h"

using sampler::Action;
using sampler::InputMode;
using sampler::LearnResult;
using sampler::SetupLearner;

namespace {
constexpr const char* kNano = "nano:nano MIDI 1 20:0";
constexpr const char* kPad = "Launchpad:Launchpad MIDI 1 24:0";
}

TEST_CASE("keys are asked in the order next, prev, toggle, quit") {
    SetupLearner learner(InputMode::Keyboard);
    CHECK(learner.current() == Action::Next);
    CHECK(learner.offerKey("\x1b[C") == LearnResult::Accepted);
    CHECK(learner.current() == Action::Prev);
    CHECK(learner.offerKey("\x1b[D") == LearnResult::Accepted);
    CHECK(learner.current() == Action::Toggle);
    CHECK(learner.offerKey(" ") == LearnResult::Accepted);
    CHECK(learner.current() == Action::Quit);
    CHECK_FALSE(learner.done());
    CHECK(learner.offerKey("x") == LearnResult::Accepted);
    CHECK(learner.done());

    auto c = learner.result();
    CHECK(c.input == InputMode::Keyboard);
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Prev).name == "left");
    CHECK(c.keys.at(Action::Toggle).name == "space");
    CHECK(c.keys.at(Action::Quit).bytes == "x");
}

TEST_CASE("a key already taken is rejected and the same action is asked again") {
    SetupLearner learner(InputMode::Keyboard);
    learner.offerKey("a");
    CHECK(learner.offerKey("a") == LearnResult::Duplicate);
    CHECK(learner.conflict() == Action::Next);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("both sequences of an arrow count as the same key") {
    SetupLearner learner(InputMode::Keyboard);
    learner.offerKey("\x1b[C");
    CHECK(learner.offerKey("\x1bOC") == LearnResult::Duplicate);
}

TEST_CASE("an unnamed key is unsupported and does not advance") {
    SetupLearner learner(InputMode::Keyboard);
    CHECK(learner.offerKey("\x1bOP") == LearnResult::Unsupported);
    CHECK(learner.current() == Action::Next);
}

TEST_CASE("the first midi press picks the device and other devices are ignored") {
    SetupLearner learner(InputMode::Midi);
    CHECK(learner.offerMidi(kNano, {0xB0, 44, 127}) == LearnResult::Accepted);
    CHECK(learner.offerMidi(kPad, {0x90, 60, 100}) == LearnResult::Ignored);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("midi releases do not advance") {
    SetupLearner learner(InputMode::Midi);
    CHECK(learner.offerMidi(kNano, {0x90, 60, 100}) == LearnResult::Accepted);
    CHECK(learner.offerMidi(kNano, {0x80, 60, 0}) == LearnResult::Ignored);
    CHECK(learner.offerMidi(kNano, {0x90, 60, 0}) == LearnResult::Ignored);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("a midi control already taken is rejected") {
    SetupLearner learner(InputMode::Midi);
    learner.offerMidi(kNano, {0xB0, 44, 127});
    CHECK(learner.offerMidi(kNano, {0xB0, 44, 64}) == LearnResult::Duplicate);
    CHECK(learner.conflict() == Action::Next);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("a finished midi setup keeps the device without the alsa suffix and the backup keys") {
    SetupLearner learner(InputMode::Midi);
    learner.offerMidi(kNano, {0xB0, 44, 127});
    learner.offerMidi(kNano, {0xB0, 43, 127});
    learner.offerMidi(kNano, {0x90, 60, 100});
    learner.offerMidi(kNano, {0xB0, 46, 127});
    REQUIRE(learner.done());

    auto c = learner.result();
    const sampler::MidiBinding toggle{sampler::MidiKind::Note, 1, 60};
    CHECK(c.input == InputMode::Midi);
    CHECK(c.midiPort == "nano:nano MIDI 1");
    CHECK(c.midi.size() == 4);
    CHECK(c.midi.at(Action::Toggle) == toggle);
    CHECK(c.keys.at(Action::Next).name == "right");
}
