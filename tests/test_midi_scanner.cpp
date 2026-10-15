#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "RtMidi.h"
#include "adapters/midi_scanner.h"
#include "doctest.h"

namespace {
constexpr const char* kVirtualPort = "sampler-test-scanner";
}

TEST_CASE("the scanner hears every port and says which one spoke") {
    std::unique_ptr<RtMidiOut> out;
    try {
        out = std::make_unique<RtMidiOut>();
        out->openVirtualPort(kVirtualPort);
    } catch (const RtMidiError&) {
        MESSAGE("no MIDI system here, skipping");
        return;
    }

    sampler::MidiScanner scanner;
    REQUIRE(scanner.open() >= 1);

    std::vector<unsigned char> press{0x90, 60, 100};
    out->sendMessage(&press);

    sampler::MidiEvent event;
    bool heard = false;
    for (int i = 0; i < 100 && !heard; ++i) {
        heard = scanner.next(event);
        if (!heard) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(heard);
    CHECK(event.port.find(kVirtualPort) != std::string::npos);
    CHECK(event.bytes == press);
}
