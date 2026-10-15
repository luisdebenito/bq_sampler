#include <map>
#include <vector>

#include "doctest.h"
#include "midi_binding.h"

using sampler::Action;
using sampler::MidiBinding;
using sampler::MidiKind;

static std::map<Action, MidiBinding> bindings() {
    return {
        {Action::Next, {MidiKind::Cc, 1, 44}},
        {Action::Toggle, {MidiKind::Note, 1, 60}},
    };
}

TEST_CASE("a note on with velocity becomes a note binding on its channel") {
    MidiBinding b;
    REQUIRE(sampler::midiBindingFromMessage({0x90, 60, 100}, b));
    CHECK(b.kind == MidiKind::Note);
    CHECK(b.channel == 1);
    CHECK(b.number == 60);
}

TEST_CASE("the channel is read from the low nibble, 1 to 16") {
    MidiBinding b;
    REQUIRE(sampler::midiBindingFromMessage({0xBF, 20, 127}, b));
    CHECK(b.kind == MidiKind::Cc);
    CHECK(b.channel == 16);
    CHECK(b.number == 20);
}

TEST_CASE("releases are not presses") {
    MidiBinding b;
    CHECK_FALSE(sampler::midiBindingFromMessage({0x90, 60, 0}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0x80, 60, 64}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0xB0, 20, 0}, b));
}

TEST_CASE("pressure, pitch bend and empty messages are not presses") {
    MidiBinding b;
    CHECK_FALSE(sampler::midiBindingFromMessage({0xA0, 60, 90}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0xD0, 90}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0xE0, 0, 64}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({}, b));
}

TEST_CASE("a bound press maps to its action") {
    Action a;
    REQUIRE(sampler::matchMidi(bindings(), {0xB0, 44, 127}, a));
    CHECK(a == Action::Next);
    REQUIRE(sampler::matchMidi(bindings(), {0x90, 60, 1}, a));
    CHECK(a == Action::Toggle);
}

TEST_CASE("the same number on another channel or of another kind does not match") {
    Action a;
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xB1, 44, 127}, a));
    CHECK_FALSE(sampler::matchMidi(bindings(), {0x90, 44, 127}, a));
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xB0, 60, 127}, a));
}

TEST_CASE("a release of a bound control does not fire") {
    Action a;
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xB0, 44, 0}, a));
    CHECK_FALSE(sampler::matchMidi(bindings(), {0x90, 60, 0}, a));
}

TEST_CASE("pressure on a bound note does not fire") {
    Action a;
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xA0, 60, 90}, a));
}

TEST_CASE("the alsa client and port numbers are ignored when comparing") {
    CHECK(sampler::stripPortSuffix("nano:nano MIDI 1 20:0") == "nano:nano MIDI 1");
    CHECK(sampler::samePort("nano:nano MIDI 1", "nano:nano MIDI 1 20:0"));
    CHECK(sampler::samePort("nano:nano MIDI 1 20:0", "nano:nano MIDI 1 24:0"));
}

TEST_CASE("different devices are different ports") {
    CHECK_FALSE(sampler::samePort("nanoKONTROL2", "Launchpad Mini"));
    CHECK_FALSE(sampler::samePort("", ""));
}

TEST_CASE("names without an alsa suffix are compared as they are") {
    CHECK(sampler::stripPortSuffix("Device 7") == "Device 7");
    CHECK(sampler::stripPortSuffix("Foo Bar:Baz") == "Foo Bar:Baz");
    CHECK(sampler::samePort("nanoKONTROL2 SLIDER/KNOB", "nanoKONTROL2 SLIDER/KNOB"));
}
