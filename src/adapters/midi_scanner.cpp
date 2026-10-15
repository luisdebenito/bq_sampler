#include "adapters/midi_scanner.h"

#include "RtMidi.h"

namespace sampler {

MidiScanner::MidiScanner() = default;

MidiScanner::~MidiScanner() = default;

size_t MidiScanner::open() {
    try {
        RtMidiIn probe;
        for (unsigned int i = 0; i < probe.getPortCount(); ++i) {
            // one busy port must not hide the others
            try {
                auto in = std::make_unique<RtMidiIn>();
                in->openPort(i);
                names_.push_back(probe.getPortName(i));
                inputs_.push_back(std::move(in));
            } catch (const RtMidiError&) {
            }
        }
    } catch (const RtMidiError&) {
    }
    return inputs_.size();
}

bool MidiScanner::next(MidiEvent& out) {
    for (size_t i = 0; i < inputs_.size(); ++i) {
        inputs_[i]->getMessage(&out.bytes);
        if (out.bytes.empty()) continue;
        out.port = names_[i];
        return true;
    }
    return false;
}

}  // namespace sampler
