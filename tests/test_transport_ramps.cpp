#include "doctest.h"
#include "domain/transport.h"
#include "support/fake_library.h"

using sampler::Transport;
using sampler::kChannels;

TEST_CASE("starting playback fades in over rampFrames") {
    auto lib = makeLib({8});
    Transport t(1, 4);
    t.toggle();
    float out[4 * kChannels];
    t.render(out, 4, lib);
    CHECK(out[0] == doctest::Approx(0.100f * 0.25f));
    CHECK(out[2] == doctest::Approx(0.101f * 0.50f));
    CHECK(out[4] == doctest::Approx(0.102f * 0.75f));
    CHECK(out[6] == doctest::Approx(0.103f * 1.00f));
}

TEST_CASE("a loop repeating on itself gets no ramp") {
    auto lib = makeLib({4});
    Transport t(1, 2);
    t.toggle();
    float out[8 * kChannels];
    t.render(out, 8, lib);
    CHECK(out[8] == doctest::Approx(0.100f));   // 2a vuelta, frame 0, ganancia plena
    CHECK(out[10] == doctest::Approx(0.101f));
}

TEST_CASE("switching samples fades out the old one and fades in the new one") {
    auto lib = makeLib({6, 6});
    Transport t(2, 2);
    t.toggle();
    float out[6 * kChannels];
    t.render(out, 2, lib);     // fundido de entrada de s0, frames 0 y 1
    t.step(+1);
    t.render(out, 6, lib);     // frames 2..5 de s0, luego frames 0 y 1 de s1
    CHECK(out[0] == doctest::Approx(0.102f));           // ganancia plena
    CHECK(out[2] == doctest::Approx(0.103f));
    CHECK(out[4] == doctest::Approx(0.104f * 0.5f));    // empieza el fundido de salida
    CHECK(out[6] == doctest::Approx(0.0f));             // ultimo frame de s0, a cero
    CHECK(out[8] == doctest::Approx(0.200f * 0.5f));    // s1 frame 0, fundido de entrada
    CHECK(out[10] == doctest::Approx(0.201f * 1.0f));
}

TEST_CASE("stopping fades out instead of cutting") {
    auto lib = makeLib({16});
    Transport t(1, 2);
    t.toggle();
    float out[4 * kChannels];
    t.render(out, 2, lib);
    t.toggle();
    t.render(out, 4, lib);
    CHECK(out[0] == doctest::Approx(0.102f * 0.5f));
    CHECK(out[2] == doctest::Approx(0.0f));
    CHECK(out[4] == doctest::Approx(0.0f));
    CHECK(out[6] == doctest::Approx(0.0f));
}

TEST_CASE("after the fade out the playhead is back at zero") {
    auto lib = makeLib({16});
    Transport t(1, 2);
    t.toggle();
    float out[4 * kChannels];
    t.render(out, 4, lib);
    t.toggle();
    t.render(out, 4, lib);
    t.toggle();
    t.render(out, 1, lib);
    CHECK(out[0] == doctest::Approx(0.100f * 0.5f));
}

// Review Focus 3: un sample mas corto que la rampa no puede desbordar ni salirse de [0,1]
TEST_CASE("a sample shorter than the ramp stays within bounds") {
    auto lib = makeLib({2, 2});
    Transport t(2, 8);
    t.toggle();
    t.step(+1);
    float out[16 * kChannels];
    t.render(out, 16, lib);
    for (uint32_t i = 0; i < 16 * kChannels; ++i) {
        CHECK(out[i] >= 0.0f);
        CHECK(out[i] <= 0.3f);
    }
}

TEST_CASE("cancelling a pending switch restores the gain instead of going silent") {
    auto lib = makeLib({8, 8});
    Transport t(2, 4);
    t.toggle();
    float out[8 * kChannels];
    t.render(out, 4, lib);     // fundido de entrada completo, ganancia a tope
    t.step(+1);                // armo s1: arranca el fundido de salida
    t.render(out, 2, lib);
    t.step(-1);                // cancelo: vuelvo a armar el que ya suena
    t.render(out, 8, lib);
    CHECK(out[14] == doctest::Approx(0.105f));
}

TEST_CASE("jump fades out the old sample before starting the new one") {
    auto lib = makeLib({8, 8});
    Transport t(2, 2);
    t.toggle();
    float out[4 * kChannels];
    t.render(out, 4, lib);
    t.step(+1);
    t.jump();
    t.render(out, 4, lib);
    CHECK(out[0] == doctest::Approx(0.104f * 0.5f));
    CHECK(out[2] == doctest::Approx(0.0f));
    CHECK(out[4] == doctest::Approx(0.200f * 0.5f));
    CHECK(out[6] == doctest::Approx(0.201f));
}

TEST_CASE("stopping in the middle of a jump stays stopped") {
    auto lib = makeLib({16, 16});
    Transport t(2, 2);
    t.toggle();
    float out[8 * kChannels];
    t.render(out, 4, lib);
    t.step(+1);
    t.jump();
    t.render(out, 1, lib);
    t.toggle();
    t.render(out, 8, lib);
    t.render(out, 4, lib);
    for (uint32_t i = 0; i < 4 * kChannels; ++i) CHECK(out[i] == 0.0f);
}
