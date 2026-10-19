#include "doctest.h"
#include "domain/transport.h"
#include "support/fake_library.h"

using sampler::Transport;
using sampler::kChannels;

TEST_CASE("the armed change lands exactly at the loop boundary") {
    auto lib = makeLib({3, 5});
    Transport t(2, 0);
    t.toggle();
    float out[3 * kChannels];
    t.render(out, 2, lib);
    t.step(+1);
    t.render(out, 3, lib);
    CHECK(out[0] == doctest::Approx(0.102f));  // s0, ultimo frame
    CHECK(out[2] == doctest::Approx(0.200f));  // s1, frame 0
    CHECK(out[4] == doctest::Approx(0.201f));  // s1, frame 1
}

TEST_CASE("arrow presses accumulate while playing") {
    auto lib = makeLib({2, 2, 2, 2});
    Transport t(4, 0);
    t.toggle();
    float out[3 * kChannels];
    t.render(out, 1, lib);     // frame 0 de s0
    t.step(+1);
    t.step(+1);
    CHECK(t.armed() == 2);
    t.render(out, 3, lib);     // frame 1 de s0, luego frames 0 y 1 de s2
    CHECK(out[0] == doctest::Approx(0.101f));
    CHECK(out[2] == doctest::Approx(0.300f));
    CHECK(out[4] == doctest::Approx(0.301f));
}

TEST_CASE("the index wraps around both ends") {
    auto lib = makeLib({2, 2, 2});
    Transport t(3, 0);
    t.step(-1);
    CHECK(t.armed() == 2);
    t.step(+1);
    CHECK(t.armed() == 0);
}

TEST_CASE("arming the sample that is already playing just restarts the loop") {
    auto lib = makeLib({2, 2});
    Transport t(2, 0);
    t.toggle();
    t.step(+1);
    t.step(-1);
    float out[3 * kChannels];
    t.render(out, 3, lib);
    CHECK(out[4] == doctest::Approx(0.100f));
}

TEST_CASE("navigating while stopped produces no audio") {
    auto lib = makeLib({2, 2});
    Transport t(2, 0);
    t.step(+1);
    float out[2 * kChannels];
    for (float& v : out) v = 9.0f;
    t.render(out, 2, lib);
    for (float v : out) CHECK(v == 0.0f);
    CHECK(t.armed() == 1);
}

TEST_CASE("play picks up the index armed while stopped") {
    auto lib = makeLib({2, 2});
    Transport t(2, 0);
    t.step(+1);
    t.toggle();
    float out[1 * kChannels];
    t.render(out, 1, lib);
    CHECK(out[0] == doctest::Approx(0.200f));
}

// Review Focus 1: con un solo sample, avanzar no puede salirse del rango
TEST_CASE("stepping with a single sample stays at index 0") {
    auto lib = makeLib({2});
    Transport t(1, 0);
    t.step(+1);
    CHECK(t.armed() == 0);
    t.step(-1);
    CHECK(t.armed() == 0);
    t.toggle();
    float out[3 * kChannels];
    t.render(out, 3, lib);
    CHECK(out[4] == doctest::Approx(0.100f));
}

TEST_CASE("jump switches to the armed sample without waiting for the loop end") {
    auto lib = makeLib({5, 3});
    Transport t(2, 0);
    t.toggle();
    float out[2 * kChannels];
    t.render(out, 1, lib);
    t.step(+1);
    t.jump();
    t.render(out, 2, lib);
    CHECK(out[0] == doctest::Approx(0.200f));
    CHECK(out[2] == doctest::Approx(0.201f));
}

TEST_CASE("jump with nothing else armed restarts the current sample") {
    auto lib = makeLib({5});
    Transport t(1, 0);
    t.toggle();
    float out[3 * kChannels];
    t.render(out, 3, lib);
    t.jump();
    t.render(out, 1, lib);
    CHECK(out[0] == doctest::Approx(0.100f));
}

TEST_CASE("jump while stopped does nothing to the next play") {
    auto lib = makeLib({4, 4});
    Transport t(2, 0);
    t.jump();
    float out[3 * kChannels];
    t.render(out, 1, lib);
    t.toggle();
    t.render(out, 3, lib);
    CHECK(out[0] == doctest::Approx(0.100f));
    CHECK(out[4] == doctest::Approx(0.102f));
}
