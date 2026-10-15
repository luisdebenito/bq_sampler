#include <cstring>

#include "adapters/termios_keyboard.h"
#include "doctest.h"

using sampler::Action;

static bool match(const sampler::Config& c, const char* s, Action& a) {
    return sampler::matchAction(c, s, std::strlen(s), a);
}

TEST_CASE("the standard arrow sequences map to prev and next") {
    auto c = sampler::defaultConfig();
    Action a;
    REQUIRE(match(c, "\x1b[C", a));
    CHECK(a == Action::Next);
    REQUIRE(match(c, "\x1b[D", a));
    CHECK(a == Action::Prev);
}

TEST_CASE("the application mode arrow sequences map to the same actions") {
    auto c = sampler::defaultConfig();
    Action a;
    REQUIRE(match(c, "\x1bOC", a));
    CHECK(a == Action::Next);
    REQUIRE(match(c, "\x1bOD", a));
    CHECK(a == Action::Prev);
}

TEST_CASE("space toggles and q quits") {
    auto c = sampler::defaultConfig();
    Action a;
    REQUIRE(match(c, " ", a));
    CHECK(a == Action::Toggle);
    REQUIRE(match(c, "q", a));
    CHECK(a == Action::Quit);
}

TEST_CASE("an unbound key matches nothing") {
    auto c = sampler::defaultConfig();
    Action a;
    CHECK(match(c, "z", a) == false);
    CHECK(match(c, "\x1b[Z", a) == false);
}

TEST_CASE("a rebound key is honoured and the old one stops working") {
    auto c = sampler::defaultConfig();
    sampler::KeyBinding b;
    sampler::keyNameToBytes("d", b);
    c.keys[Action::Next] = b;
    Action a;
    REQUIRE(match(c, "d", a));
    CHECK(a == Action::Next);
    CHECK(match(c, "\x1b[C", a) == false);
}

TEST_CASE("a key bound to enter fires on the newline the terminal sends") {
    auto c = sampler::defaultConfig();
    sampler::KeyBinding b;
    sampler::keyNameToBytes("enter", b);
    c.keys[Action::Toggle] = b;
    Action a;
    REQUIRE(match(c, "\n", a));
    CHECK(a == Action::Toggle);
}
