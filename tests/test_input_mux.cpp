#include <unistd.h>

#include "adapters/input_mux.h"
#include "doctest.h"

using sampler::Action;

namespace {

struct PipeInput : sampler::FdInput {
    int ends[2];
    Action action;
    bool understands = true;

    explicit PipeInput(Action a) : action(a) { REQUIRE(::pipe(ends) == 0); }
    ~PipeInput() override {
        ::close(ends[0]);
        ::close(ends[1]);
    }

    int fd() const override { return ends[0]; }

    bool readAction(Action& out) override {
        char byte;
        if (::read(ends[0], &byte, 1) != 1) return false;
        if (!understands) return false;
        out = action;
        return true;
    }

    void press() {
        const char byte = 0;
        REQUIRE(::write(ends[1], &byte, 1) == 1);
    }
};

}  // namespace

TEST_CASE("with nothing pressed the mux times out without an action") {
    PipeInput keys(Action::Next);
    sampler::InputMux mux({&keys});
    Action a;
    CHECK_FALSE(mux.poll(0, a));
}

TEST_CASE("the mux wakes on whichever input has data") {
    PipeInput keys(Action::Next);
    PipeInput midi(Action::Toggle);
    sampler::InputMux mux({&keys, &midi});
    Action a;
    midi.press();
    REQUIRE(mux.poll(1000, a));
    CHECK(a == Action::Toggle);
    keys.press();
    REQUIRE(mux.poll(1000, a));
    CHECK(a == Action::Next);
    CHECK_FALSE(mux.poll(0, a));
}

TEST_CASE("two quick presses on one input come out as two actions") {
    PipeInput midi(Action::Next);
    sampler::InputMux mux({&midi});
    midi.press();
    midi.press();
    Action a;
    CHECK(mux.poll(1000, a));
    CHECK(mux.poll(1000, a));
    CHECK_FALSE(mux.poll(0, a));
}

TEST_CASE("garbage on one input does not swallow a press on the other") {
    PipeInput keys(Action::Prev);
    PipeInput midi(Action::Toggle);
    keys.understands = false;
    sampler::InputMux mux({&keys, &midi});
    keys.press();
    midi.press();
    Action a;
    REQUIRE(mux.poll(1000, a));
    CHECK(a == Action::Toggle);
    CHECK_FALSE(mux.poll(0, a));
}
