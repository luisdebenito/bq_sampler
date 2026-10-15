#include <filesystem>
#include <fstream>

#include "config.h"
#include "doctest.h"

using sampler::Action;

TEST_CASE("default config binds the three keys plus quit") {
    auto c = sampler::defaultConfig();
    CHECK(c.keys.at(Action::Prev).name == "left");
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Toggle).name == "space");
    CHECK(c.keys.at(Action::Quit).name == "q");
    CHECK(c.rampMs == sampler::kDefaultRampMs);
    CHECK(c.blinkMs == sampler::kDefaultBlinkMs);
}

TEST_CASE("arrow keys carry both escape sequences") {
    sampler::KeyBinding b;
    REQUIRE(sampler::keyNameToBytes("right", b));
    CHECK(b.bytes == "\x1b[C");
    CHECK(b.altBytes == "\x1bOC");
}

TEST_CASE("a single printable character binds to itself") {
    sampler::KeyBinding b;
    REQUIRE(sampler::keyNameToBytes("d", b));
    CHECK(b.bytes == "d");
    CHECK(b.altBytes.empty());
}

TEST_CASE("an unknown key name is rejected") {
    sampler::KeyBinding b;
    CHECK(sampler::keyNameToBytes("f13", b) == false);
}

TEST_CASE("a config file overrides the defaults it mentions") {
    auto path = std::filesystem::temp_directory_path() / "sampler_test.conf";
    {
        std::ofstream f(path);
        f << "# comentario\n"
          << "prev = a\n"
          << "next = d\n"
          << "blink_ms = 400\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.keys.at(Action::Prev).bytes == "a");
    CHECK(c.keys.at(Action::Next).bytes == "d");
    CHECK(c.keys.at(Action::Toggle).name == "space");
    CHECK(c.blinkMs == 400);
    CHECK(c.rampMs == sampler::kDefaultRampMs);
    std::filesystem::remove(path);
}

TEST_CASE("a missing config file yields the defaults") {
    auto c = sampler::loadConfig("/definitely/not/here.conf");
    CHECK(c.keys.at(Action::Toggle).name == "space");
}

TEST_CASE("an unparseable line is ignored, the rest still applies") {
    auto path = std::filesystem::temp_directory_path() / "sampler_bad.conf";
    {
        std::ofstream f(path);
        f << "garbage without equals\n"
          << "next = f13\n"
          << "quit = x\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Quit).bytes == "x");
    std::filesystem::remove(path);
}

TEST_CASE("a negative or non numeric number falls back to the default") {
    auto path = std::filesystem::temp_directory_path() / "sampler_nums.conf";
    {
        std::ofstream f(path);
        f << "ramp_ms = -2\n"
          << "blink_ms = dos\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.rampMs == sampler::kDefaultRampMs);
    CHECK(c.blinkMs == sampler::kDefaultBlinkMs);
    std::filesystem::remove(path);
}

TEST_CASE("key bytes turn back into the name that produced them") {
    std::string name;
    REQUIRE(sampler::keyNameFromBytes("\x1b[C", name));
    CHECK(name == "right");
    REQUIRE(sampler::keyNameFromBytes("\x1bOD", name));
    CHECK(name == "left");
    REQUIRE(sampler::keyNameFromBytes(" ", name));
    CHECK(name == "space");
    REQUIRE(sampler::keyNameFromBytes("d", name));
    CHECK(name == "d");
}

TEST_CASE("keys without a name are not supported") {
    std::string name;
    CHECK_FALSE(sampler::keyNameFromBytes("\x1bOP", name));
    CHECK_FALSE(sampler::keyNameFromBytes("\t", name));
    CHECK_FALSE(sampler::keyNameFromBytes("ab", name));
    CHECK_FALSE(sampler::keyNameFromBytes("left", name));
}

TEST_CASE("every named key survives the round trip through its bytes") {
    for (const char* key : {"left", "right", "up", "down", "space", "enter", "esc", "q"}) {
        sampler::KeyBinding b;
        REQUIRE(sampler::keyNameToBytes(key, b));
        std::string name;
        REQUIRE(sampler::keyNameFromBytes(b.bytes, name));
        CHECK(name == key);
    }
}

TEST_CASE("an existing config without an input line stays in keyboard mode") {
    auto path = std::filesystem::temp_directory_path() / "sampler_legacy.conf";
    {
        std::ofstream f(path);
        f << "next = d\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.input == sampler::InputMode::Keyboard);
    CHECK(c.midi.empty());
    std::filesystem::remove(path);
}

TEST_CASE("midi lines load into midi bindings and the default keys stay as backup") {
    auto path = std::filesystem::temp_directory_path() / "sampler_midi.conf";
    {
        std::ofstream f(path);
        f << "input = midi\n"
          << "midi_port = nanoKONTROL2\n"
          << "midi_next = cc:1:44\n"
          << "midi_toggle = note:10:60\n";
    }
    auto c = sampler::loadConfig(path.string());
    const sampler::MidiBinding next{sampler::MidiKind::Cc, 1, 44};
    const sampler::MidiBinding toggle{sampler::MidiKind::Note, 10, 60};
    CHECK(c.input == sampler::InputMode::Midi);
    CHECK(c.midiPort == "nanoKONTROL2");
    CHECK(c.midi.at(Action::Next) == next);
    CHECK(c.midi.at(Action::Toggle) == toggle);
    CHECK(c.midi.count(Action::Quit) == 0);
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Quit).name == "q");
    std::filesystem::remove(path);
}

TEST_CASE("out of range or malformed midi bindings are ignored") {
    auto path = std::filesystem::temp_directory_path() / "sampler_midi_bad.conf";
    {
        std::ofstream f(path);
        f << "input = midi\n"
          << "midi_next = cc:17:44\n"
          << "midi_prev = note:1:200\n"
          << "midi_toggle = pad:1:3\n"
          << "midi_quit = cc:1\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.midi.empty());
    std::filesystem::remove(path);
}

TEST_CASE("an unknown input value leaves keyboard mode") {
    auto path = std::filesystem::temp_directory_path() / "sampler_input_bad.conf";
    {
        std::ofstream f(path);
        f << "input = MIDI\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.input == sampler::InputMode::Keyboard);
    std::filesystem::remove(path);
}

TEST_CASE("a saved keyboard config loads back the same") {
    auto c = sampler::defaultConfig();
    sampler::KeyBinding d;
    sampler::keyNameToBytes("d", d);
    c.keys[Action::Next] = d;
    c.blinkMs = 400;
    auto path = std::filesystem::temp_directory_path() / "sampler_saved_keys.conf";
    REQUIRE(sampler::saveConfig(path.string(), c));
    auto back = sampler::loadConfig(path.string());
    CHECK(back.input == sampler::InputMode::Keyboard);
    for (const auto& [action, binding] : c.keys) {
        CHECK(back.keys.at(action).name == binding.name);
    }
    CHECK(back.blinkMs == 400);
    CHECK(back.rampMs == c.rampMs);
    std::filesystem::remove(path);
}

TEST_CASE("a saved midi config loads back the same") {
    auto c = sampler::defaultConfig();
    c.input = sampler::InputMode::Midi;
    c.midiPort = "nano:nano MIDI 1";
    c.midi[Action::Next] = {sampler::MidiKind::Cc, 1, 44};
    c.midi[Action::Prev] = {sampler::MidiKind::Cc, 1, 43};
    c.midi[Action::Toggle] = {sampler::MidiKind::Note, 16, 0};
    c.midi[Action::Quit] = {sampler::MidiKind::Cc, 2, 127};
    auto path = std::filesystem::temp_directory_path() / "sampler_saved_midi.conf";
    REQUIRE(sampler::saveConfig(path.string(), c));
    auto back = sampler::loadConfig(path.string());
    CHECK(back.input == sampler::InputMode::Midi);
    CHECK(back.midiPort == c.midiPort);
    REQUIRE(back.midi.size() == 4);
    for (const auto& [action, binding] : c.midi) {
        CHECK(back.midi.at(action) == binding);
    }
    CHECK(back.keys.at(Action::Toggle).name == "space");
    std::filesystem::remove(path);
}

TEST_CASE("saving into a folder that does not exist reports failure") {
    CHECK_FALSE(sampler::saveConfig("/definitely/not/here/sampler.conf", sampler::defaultConfig()));
}

TEST_CASE("enter is recognised as the newline a terminal in raw mode delivers") {
    std::string name;
    REQUIRE(sampler::keyNameFromBytes("\n", name));
    CHECK(name == "enter");
}
