#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class RtMidiIn;

namespace sampler {

struct MidiEvent {
    std::string port;
    std::vector<unsigned char> bytes;
};

class MidiScanner {
public:
    MidiScanner();
    ~MidiScanner();

    size_t open();
    bool next(MidiEvent& out);

private:
    std::vector<std::unique_ptr<RtMidiIn>> inputs_;
    std::vector<std::string> names_;
};

}  // namespace sampler
