#include <poll.h>

#include <memory>
#include <string>
#include <vector>

#include "RtMidi.h"
#include "adapters/rtmidi_input.h"
#include "doctest.h"

using sampler::Action;

namespace {

constexpr const char* kVirtualPort = "sampler-test-input";

std::string findPort(const std::string& fragment) {
    RtMidiIn probe;
    for (unsigned int i = 0; i < probe.getPortCount(); ++i) {
        const std::string name = probe.getPortName(i);
        if (name.find(fragment) != std::string::npos) return name;
    }
    return "";
}

}  // namespace

TEST_CASE("a bound press on the saved port comes out of the fd as its action") {
    std::unique_ptr<RtMidiOut> out;
    try {
        out = std::make_unique<RtMidiOut>();
        out->openVirtualPort(kVirtualPort);
    } catch (const RtMidiError&) {
        MESSAGE("no MIDI system here, skipping");
        return;
    }

    auto config = sampler::defaultConfig();
    config.input = sampler::InputMode::Midi;
    config.midiPort = sampler::stripPortSuffix(findPort(kVirtualPort));
    config.midi[Action::Next] = {sampler::MidiKind::Cc, 2, 20};

    sampler::RtMidiInput input(config);
    REQUIRE(input.open());

    std::vector<unsigned char> release{0xB1, 20, 0};
    std::vector<unsigned char> press{0xB1, 20, 127};
    out->sendMessage(&release);
    out->sendMessage(&press);

    pollfd fd{input.fd(), POLLIN, 0};
    REQUIRE(::poll(&fd, 1, 1000) == 1);
    Action a;
    REQUIRE(input.readAction(a));
    CHECK(a == Action::Next);
    CHECK_FALSE(input.readAction(a));
}

TEST_CASE("a controller that is not connected fails to open instead of throwing") {
    auto config = sampler::defaultConfig();
    config.input = sampler::InputMode::Midi;
    config.midiPort = "no such controller";
    sampler::RtMidiInput input(config);
    CHECK_FALSE(input.open());
}
