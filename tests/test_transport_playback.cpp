#include "doctest.h"
#include "domain/transport.h"
#include "support/fake_library.h"

using sampler::Transport;
using sampler::kChannels;

TEST_CASE("stopped outputs silence") {
    auto lib = makeLib({4});
    Transport t(1, 0);
    float out[4 * kChannels];
    for (float& v : out) v = 9.0f;
    t.render(out, 4, lib);
    for (float v : out) CHECK(v == 0.0f);
}

TEST_CASE("play starts at frame 0 of the current sample") {
    auto lib = makeLib({4});
    Transport t(1, 0);
    t.toggle();
    float out[2 * kChannels];
    t.render(out, 2, lib);
    CHECK(out[0] == doctest::Approx(0.100f));
    CHECK(out[2] == doctest::Approx(0.101f));
}

TEST_CASE("the sample loops back to its own frame 0") {
    auto lib = makeLib({2});
    Transport t(1, 0);
    t.toggle();
    float out[5 * kChannels];
    t.render(out, 5, lib);
    CHECK(out[0] == doctest::Approx(0.100f));
    CHECK(out[2] == doctest::Approx(0.101f));
    CHECK(out[4] == doctest::Approx(0.100f));
    CHECK(out[6] == doctest::Approx(0.101f));
    CHECK(out[8] == doctest::Approx(0.100f));
}

TEST_CASE("stop resets the playhead and the next play starts at 0") {
    auto lib = makeLib({4});
    Transport t(1, 0);
    t.toggle();
    float out[2 * kChannels];
    t.render(out, 2, lib);
    t.toggle();
    t.render(out, 2, lib);
    CHECK(out[0] == 0.0f);
    t.toggle();
    t.render(out, 2, lib);
    CHECK(out[0] == doctest::Approx(0.100f));
}

TEST_CASE("toggle flips the reported playing state") {
    auto lib = makeLib({4});
    Transport t(1, 0);
    CHECK(t.isPlaying() == false);
    t.toggle();
    CHECK(t.isPlaying() == true);
    t.toggle();
    CHECK(t.isPlaying() == false);
}

// Review Focus 2: una librería vacía no puede indexar ni dividir
TEST_CASE("an empty library renders silence instead of crashing") {
    sampler::SampleLibrary lib;
    lib.sampleRate = 48000;
    Transport t(0, 0);
    t.toggle();
    float out[4 * kChannels];
    for (float& v : out) v = 9.0f;
    t.render(out, 4, lib);
    for (float v : out) CHECK(v == 0.0f);
}
