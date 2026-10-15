# Entrada MIDI y asistente de primer arranque: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Poder tocar el sampler con un controlador MIDI además de con el teclado, configurándolo con un asistente que aparece cuando no existe `sampler.conf`.

**Architecture:** El puerto `InputSource` no cambia. Teclado y MIDI pasan a ser `FdInput` (un descriptor más una lectura sin bloqueo) y un `InputMux` hace un único `poll()` sobre los dos. El callback de RtMidi corre en su propio hilo y solo escribe un byte en una tubería (self-pipe), así que únicamente el hilo principal toca `Transport`. El asistente separa la lógica pura (`SetupLearner`, con tests) del bucle interactivo (`setup_wizard`, fino y sin test, como `main`).

**Tech Stack:** C++17, RtMidi 6.0.0 (dos ficheros en `third_party/`; CoreMIDI en macOS, secuenciador ALSA en Linux), doctest, Makefile con `uname -s`.

**Spec:** `docs/superpowers/specs/2026-10-15-midi-input-design.md` (y el original, `docs/superpowers/specs/2026-10-14-sampler-loop-cpp-design.md`, cuyas reglas siguen vigentes).

**Desviaciones menores del spec**, todas de organización de ficheros y no de comportamiento:

- Las funciones MIDI puras (`midiBindingFromMessage`, `matchMidi`, `samePort`) van en `src/midi_binding.h/.cpp` en vez de en `config`, para que `config.cpp` no crezca con lógica que no es de formato de fichero. El parseo y formateo de `note:1:60` sí va en `config.cpp`, porque es formato de fichero y reutiliza `parseUint`.
- `SetupLearner` va en su propio `src/setup_learner.h/.cpp`, separado del bucle interactivo `src/setup_wizard.h/.cpp`.

## Global Constraints

- C++17. macOS y Linux. Windows fuera de alcance.
- RtMidi **6.0.0** exacta, en `third_party/RtMidi.h` y `third_party/RtMidi.cpp`. SHA-256: `RtMidi.h` = `74f98ca172243b0b9c0c59f53c4e20285d879ffa86b3d61692109b013f4606f9`, `RtMidi.cpp` = `184d775e88e5d73b079897363fb42af155122e879f0bc9254a1c7edae4eadb3c`.
- Excepción consciente a "cero dependencias instaladas": en Linux hace falta `libasound2-dev` para compilar. En macOS no se instala nada.
- Solo el hilo principal llama a `Transport`. El callback de RtMidi nunca toca `Transport`.
- Ninguna excepción de RtMidi sale de un adaptador: se captura dentro y se trata como "no hay MIDI".
- Un `sampler.conf` sin línea `input` se carga en modo teclado, exactamente como hoy.
- Cuenta como pulsación MIDI: Note On con velocidad > 0, o Control Change con valor > 0. Nada más.
- Formato de binding MIDI: `tipo:canal:número`, tipo `note` o `cc`, canal 1..16, número 0..127. Fuera de rango, la línea se ignora.
- Orden del asistente: SIGUIENTE, ANTERIOR, PLAY/STOP, SALIR.
- Textos que ve el usuario: en español y sin tildes, como los mensajes que ya hay en `main.cpp`. Nombres de tests en inglés, como los existentes.
- Nada de literales sueltos en código de producción: constantes con nombre. En los tests sí se permiten.
- Comentarios: por defecto ninguno. Como mucho una línea corta con un "por qué" no evidente, y nunca más de tres en un mismo bloque.

## Review Focus

Cinco entradas que el spec no menciona y que morderían a quien lo use de verdad. Cada una tiene su test en la tarea que posee el código.

1. **Pads que mandan aftertouch o presión justo después del golpe** (`0xA0`, `0xD0`) o un pitch bend. No son pulsaciones y no deben disparar nada. (Task 1)
2. **Dos pulsaciones MIDI muy seguidas** tienen que dar dos pasos, igual que dos flechas seguidas. No se puede perder la segunda. (Task 3)
3. **Basura en stdin a la vez que llega MIDI.** Una tecla sin asignar no puede tragarse la pulsación MIDI que llega en el mismo `poll()`. (Task 3)
4. **El asistente guarda en una carpeta que no existe** (`./build/sampler samples conf/live.conf` sin `conf/`). `saveConfig` devuelve `false` y el programa sigue con lo aprendido. (Task 2)
5. **El teclado de respaldo en modo MIDI.** Un config con `input = midi` y sin líneas de teclado tiene que conservar las teclas por defecto. Si no, el plan B de directo no existe. (Task 2 y Task 6)

---

### Task 1: Bindings MIDI puros

**Files:**
- Create: `src/midi_binding.h`
- Create: `src/midi_binding.cpp`
- Create: `tests/test_midi_binding.cpp`
- Modify: `Makefile` (líneas `APP_SRCS` y `TEST_SRCS`)

**Interfaces:**
- Consumes: `sampler::Action` de `src/domain/action.h`.
- Produces:
  - `enum class MidiKind { Note, Cc };`
  - `struct MidiBinding { MidiKind kind; uint8_t channel; uint8_t number; };` y `bool operator==(const MidiBinding&, const MidiBinding&)`
  - `constexpr uint8_t kMinMidiChannel = 1, kMaxMidiChannel = 16, kMaxMidiDataValue = 127;`
  - `bool midiBindingFromMessage(const std::vector<unsigned char>& message, MidiBinding& out);`
  - `bool matchMidi(const std::map<Action, MidiBinding>& bindings, const std::vector<unsigned char>& message, Action& outAction);`
  - `std::string stripPortSuffix(const std::string& name);`
  - `bool samePort(const std::string& saved, const std::string& actual);`

- [ ] **Step 1: Escribir los tests**

`tests/test_midi_binding.cpp`:

```cpp
#include <map>
#include <vector>

#include "doctest.h"
#include "midi_binding.h"

using sampler::Action;
using sampler::MidiBinding;
using sampler::MidiKind;

static std::map<Action, MidiBinding> bindings() {
    return {
        {Action::Next, {MidiKind::Cc, 1, 44}},
        {Action::Toggle, {MidiKind::Note, 1, 60}},
    };
}

TEST_CASE("a note on with velocity becomes a note binding on its channel") {
    MidiBinding b;
    REQUIRE(sampler::midiBindingFromMessage({0x90, 60, 100}, b));
    CHECK(b.kind == MidiKind::Note);
    CHECK(b.channel == 1);
    CHECK(b.number == 60);
}

TEST_CASE("the channel is read from the low nibble, 1 to 16") {
    MidiBinding b;
    REQUIRE(sampler::midiBindingFromMessage({0xBF, 20, 127}, b));
    CHECK(b.kind == MidiKind::Cc);
    CHECK(b.channel == 16);
    CHECK(b.number == 20);
}

TEST_CASE("releases are not presses") {
    MidiBinding b;
    CHECK_FALSE(sampler::midiBindingFromMessage({0x90, 60, 0}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0x80, 60, 64}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0xB0, 20, 0}, b));
}

TEST_CASE("pressure, pitch bend and empty messages are not presses") {
    MidiBinding b;
    CHECK_FALSE(sampler::midiBindingFromMessage({0xA0, 60, 90}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0xD0, 90}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({0xE0, 0, 64}, b));
    CHECK_FALSE(sampler::midiBindingFromMessage({}, b));
}

TEST_CASE("a bound press maps to its action") {
    Action a;
    REQUIRE(sampler::matchMidi(bindings(), {0xB0, 44, 127}, a));
    CHECK(a == Action::Next);
    REQUIRE(sampler::matchMidi(bindings(), {0x90, 60, 1}, a));
    CHECK(a == Action::Toggle);
}

TEST_CASE("the same number on another channel or of another kind does not match") {
    Action a;
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xB1, 44, 127}, a));
    CHECK_FALSE(sampler::matchMidi(bindings(), {0x90, 44, 127}, a));
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xB0, 60, 127}, a));
}

TEST_CASE("a release of a bound control does not fire") {
    Action a;
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xB0, 44, 0}, a));
    CHECK_FALSE(sampler::matchMidi(bindings(), {0x90, 60, 0}, a));
}

TEST_CASE("pressure on a bound note does not fire") {
    Action a;
    CHECK_FALSE(sampler::matchMidi(bindings(), {0xA0, 60, 90}, a));
}

TEST_CASE("the alsa client and port numbers are ignored when comparing") {
    CHECK(sampler::stripPortSuffix("nano:nano MIDI 1 20:0") == "nano:nano MIDI 1");
    CHECK(sampler::samePort("nano:nano MIDI 1", "nano:nano MIDI 1 20:0"));
    CHECK(sampler::samePort("nano:nano MIDI 1 20:0", "nano:nano MIDI 1 24:0"));
}

TEST_CASE("different devices are different ports") {
    CHECK_FALSE(sampler::samePort("nanoKONTROL2", "Launchpad Mini"));
    CHECK_FALSE(sampler::samePort("", ""));
}

TEST_CASE("names without an alsa suffix are compared as they are") {
    CHECK(sampler::stripPortSuffix("Device 7") == "Device 7");
    CHECK(sampler::stripPortSuffix("Foo Bar:Baz") == "Foo Bar:Baz");
    CHECK(sampler::samePort("nanoKONTROL2 SLIDER/KNOB", "nanoKONTROL2 SLIDER/KNOB"));
}
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

En `Makefile`, las dos líneas quedan así:

```make
APP_SRCS := src/config.cpp src/midi_binding.cpp
```

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp tests/test_midi_binding.cpp
```

- [ ] **Step 3: Ver que falla**

Run: `make test`
Expected: error de compilación, `'midi_binding.h' file not found`.

- [ ] **Step 4: Escribir el header**

`src/midi_binding.h`:

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "domain/action.h"

namespace sampler {

constexpr uint8_t kMinMidiChannel = 1;
constexpr uint8_t kMaxMidiChannel = 16;
constexpr uint8_t kMaxMidiDataValue = 127;

enum class MidiKind { Note, Cc };

struct MidiBinding {
    MidiKind kind = MidiKind::Note;
    uint8_t channel = kMinMidiChannel;
    uint8_t number = 0;
};

bool operator==(const MidiBinding& a, const MidiBinding& b);

bool midiBindingFromMessage(const std::vector<unsigned char>& message, MidiBinding& out);
bool matchMidi(const std::map<Action, MidiBinding>& bindings,
               const std::vector<unsigned char>& message, Action& outAction);
std::string stripPortSuffix(const std::string& name);
bool samePort(const std::string& saved, const std::string& actual);

}  // namespace sampler
```

- [ ] **Step 5: Escribir la implementación**

`src/midi_binding.cpp`:

```cpp
#include "midi_binding.h"

#include <cctype>

namespace sampler {
namespace {

constexpr unsigned char kStatusTypeMask = 0xF0;
constexpr unsigned char kChannelMask = 0x0F;
constexpr unsigned char kNoteOnStatus = 0x90;
constexpr unsigned char kControlChangeStatus = 0xB0;
constexpr unsigned char kReleaseValue = 0;
constexpr size_t kChannelMessageBytes = 3;
constexpr size_t kStatusByte = 0;
constexpr size_t kNumberByte = 1;
constexpr size_t kValueByte = 2;
constexpr char kAlsaClientPortSeparator = ':';
constexpr char kAlsaSuffixSeparator = ' ';

bool allDigits(const std::string& s, size_t from, size_t to) {
    if (from >= to) return false;
    for (size_t i = from; i < to; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

}  // namespace

bool operator==(const MidiBinding& a, const MidiBinding& b) {
    return a.kind == b.kind && a.channel == b.channel && a.number == b.number;
}

bool midiBindingFromMessage(const std::vector<unsigned char>& message, MidiBinding& out) {
    if (message.size() != kChannelMessageBytes || message[kValueByte] == kReleaseValue) {
        return false;
    }
    const unsigned char type = message[kStatusByte] & kStatusTypeMask;
    if (type == kNoteOnStatus) {
        out.kind = MidiKind::Note;
    } else if (type == kControlChangeStatus) {
        out.kind = MidiKind::Cc;
    } else {
        return false;
    }
    out.channel = static_cast<uint8_t>((message[kStatusByte] & kChannelMask) + kMinMidiChannel);
    out.number = message[kNumberByte];
    return true;
}

bool matchMidi(const std::map<Action, MidiBinding>& bindings,
               const std::vector<unsigned char>& message, Action& outAction) {
    MidiBinding pressed;
    if (!midiBindingFromMessage(message, pressed)) return false;
    for (const auto& [action, binding] : bindings) {
        if (binding == pressed) {
            outAction = action;
            return true;
        }
    }
    return false;
}

std::string stripPortSuffix(const std::string& name) {
    const size_t colon = name.rfind(kAlsaClientPortSeparator);
    if (colon == std::string::npos) return name;
    const size_t space = name.rfind(kAlsaSuffixSeparator, colon);
    if (space == std::string::npos) return name;
    if (!allDigits(name, space + 1, colon) || !allDigits(name, colon + 1, name.size())) {
        return name;
    }
    return name.substr(0, space);
}

bool samePort(const std::string& saved, const std::string& actual) {
    return !saved.empty() && stripPortSuffix(saved) == stripPortSuffix(actual);
}

}  // namespace sampler
```

- [ ] **Step 6: Ver que pasa**

Run: `make test`
Expected: todos los tests en verde, incluidos los 11 nuevos.

- [ ] **Step 7: Commit**

```bash
git add src/midi_binding.h src/midi_binding.cpp tests/test_midi_binding.cpp Makefile
git commit -m "feat: midi bindings, press detection and alsa-safe port matching"
```

---

### Task 2: Config con modo de entrada, bindings MIDI y guardado

**Files:**
- Modify: `src/config.h` (reescritura completa, abajo)
- Modify: `src/config.cpp` (reescritura completa, abajo)
- Modify: `tests/test_config.cpp` (añadir tests al final)

**Interfaces:**
- Consumes (Task 1): `MidiKind`, `MidiBinding`, `operator==`, `kMinMidiChannel`, `kMaxMidiChannel`, `kMaxMidiDataValue` de `src/midi_binding.h`.
- Produces:
  - `enum class InputMode { Keyboard, Midi };`
  - `struct Config { InputMode input; std::string midiPort; std::map<Action, KeyBinding> keys; std::map<Action, MidiBinding> midi; uint32_t rampMs; uint32_t blinkMs; };`
  - `bool keyNameFromBytes(const std::string& bytes, std::string& outName);`
  - `bool saveConfig(const std::string& path, const Config& config);` (devuelve `false` si no puede escribir)
  - `config.h` incluye `midi_binding.h`, así que quien incluya `config.h` ve los tipos MIDI.

- [ ] **Step 1: Añadir los tests al final de `tests/test_config.cpp`**

```cpp
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
```

- [ ] **Step 2: Ver que falla**

Run: `make test`
Expected: error de compilación, `no member named 'keyNameFromBytes' in namespace 'sampler'` (y parecidos para `InputMode`, `saveConfig`).

- [ ] **Step 3: Reescribir `src/config.h`**

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "domain/action.h"
#include "midi_binding.h"

namespace sampler {

constexpr uint32_t kDefaultRampMs = 2;
constexpr uint32_t kDefaultBlinkMs = 250;

enum class InputMode { Keyboard, Midi };

struct KeyBinding {
    std::string name;
    std::string bytes;
    std::string altBytes;
};

struct Config {
    InputMode input = InputMode::Keyboard;
    std::string midiPort;
    std::map<Action, KeyBinding> keys;
    std::map<Action, MidiBinding> midi;
    uint32_t rampMs = kDefaultRampMs;
    uint32_t blinkMs = kDefaultBlinkMs;
};

bool keyNameToBytes(const std::string& name, KeyBinding& out);
bool keyNameFromBytes(const std::string& bytes, std::string& outName);
Config defaultConfig();
Config loadConfig(const std::string& path);
bool saveConfig(const std::string& path, const Config& config);

}  // namespace sampler
```

- [ ] **Step 4: Reescribir `src/config.cpp`**

`trim`, `parseUint`, `keyNameToBytes` y `defaultConfig` no cambian. `actionFromName` pasa a usar la tabla `kActionNames`, que también sirve para escribir. Las claves `ramp_ms` y `blink_ms` pasan a constantes porque ahora también se escriben.

```cpp
#include "config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sampler {
namespace {

constexpr const char* kInputKey = "input";
constexpr const char* kKeyboardValue = "keyboard";
constexpr const char* kMidiValue = "midi";
constexpr const char* kMidiPortKey = "midi_port";
constexpr const char* kMidiKeyPrefix = "midi_";
constexpr const char* kRampKey = "ramp_ms";
constexpr const char* kBlinkKey = "blink_ms";
constexpr const char* kNoteName = "note";
constexpr const char* kCcName = "cc";
constexpr const char* kAssign = " = ";
constexpr char kFieldSeparator = ':';
constexpr char kLineEnd = '\n';
constexpr size_t kSingleKeyBytes = 1;
constexpr const char* kNamedKeys[] = {"left", "right", "up", "down", "space", "enter", "esc"};

struct ActionName {
    Action action;
    const char* name;
};

constexpr ActionName kActionNames[] = {
    {Action::Prev, "prev"},
    {Action::Next, "next"},
    {Action::Toggle, "toggle"},
    {Action::Quit, "quit"},
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

constexpr size_t kMaxDigits = 9;

bool parseUint(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > kMaxDigits ||
        s.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    out = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10));
    return true;
}

bool actionFromName(const std::string& name, Action& out) {
    for (const auto& entry : kActionNames) {
        if (name == entry.name) {
            out = entry.action;
            return true;
        }
    }
    return false;
}

const char* actionName(Action action) {
    for (const auto& entry : kActionNames) {
        if (entry.action == action) return entry.name;
    }
    return "";
}

bool parseMidiBinding(const std::string& text, MidiBinding& out) {
    const size_t first = text.find(kFieldSeparator);
    if (first == std::string::npos) return false;
    const size_t second = text.find(kFieldSeparator, first + 1);
    if (second == std::string::npos) return false;

    uint32_t channel = 0;
    uint32_t number = 0;
    if (!parseUint(text.substr(first + 1, second - first - 1), channel) ||
        !parseUint(text.substr(second + 1), number)) {
        return false;
    }
    if (channel < kMinMidiChannel || channel > kMaxMidiChannel || number > kMaxMidiDataValue) {
        return false;
    }

    const std::string kind = text.substr(0, first);
    if (kind == kNoteName) {
        out.kind = MidiKind::Note;
    } else if (kind == kCcName) {
        out.kind = MidiKind::Cc;
    } else {
        return false;
    }
    out.channel = static_cast<uint8_t>(channel);
    out.number = static_cast<uint8_t>(number);
    return true;
}

std::string formatMidiBinding(const MidiBinding& b) {
    return std::string(b.kind == MidiKind::Note ? kNoteName : kCcName) + kFieldSeparator +
           std::to_string(b.channel) + kFieldSeparator + std::to_string(b.number);
}

}  // namespace

bool keyNameToBytes(const std::string& name, KeyBinding& out) {
    out.name = name;
    out.altBytes.clear();
    if (name == "left")  { out.bytes = "\x1b[D"; out.altBytes = "\x1bOD"; return true; }
    if (name == "right") { out.bytes = "\x1b[C"; out.altBytes = "\x1bOC"; return true; }
    if (name == "up")    { out.bytes = "\x1b[A"; out.altBytes = "\x1bOA"; return true; }
    if (name == "down")  { out.bytes = "\x1b[B"; out.altBytes = "\x1bOB"; return true; }
    if (name == "space") { out.bytes = " ";      return true; }
    if (name == "enter") { out.bytes = "\r";     return true; }
    if (name == "esc")   { out.bytes = "\x1b";   return true; }
    if (name.size() == 1 && std::isprint(static_cast<unsigned char>(name[0]))) {
        out.bytes = name;
        return true;
    }
    return false;
}

bool keyNameFromBytes(const std::string& bytes, std::string& outName) {
    KeyBinding b;
    for (const char* name : kNamedKeys) {
        keyNameToBytes(name, b);
        if (bytes == b.bytes || (!b.altBytes.empty() && bytes == b.altBytes)) {
            outName = name;
            return true;
        }
    }
    if (bytes.size() != kSingleKeyBytes || !keyNameToBytes(bytes, b)) return false;
    outName = bytes;
    return true;
}

Config defaultConfig() {
    Config c;
    KeyBinding b;
    keyNameToBytes("left", b);   c.keys[Action::Prev] = b;
    keyNameToBytes("right", b);  c.keys[Action::Next] = b;
    keyNameToBytes("space", b);  c.keys[Action::Toggle] = b;
    keyNameToBytes("q", b);      c.keys[Action::Quit] = b;
    return c;
}

Config loadConfig(const std::string& path) {
    Config c = defaultConfig();
    std::ifstream f(path);
    if (!f) return c;

    const std::string midiPrefix = kMidiKeyPrefix;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        if (key == kRampKey) { parseUint(value, c.rampMs); continue; }
        if (key == kBlinkKey) { parseUint(value, c.blinkMs); continue; }
        if (key == kInputKey) {
            if (value == kMidiValue) c.input = InputMode::Midi;
            if (value == kKeyboardValue) c.input = InputMode::Keyboard;
            continue;
        }
        if (key == kMidiPortKey) { c.midiPort = value; continue; }

        Action action;
        if (key.compare(0, midiPrefix.size(), midiPrefix) == 0) {
            MidiBinding binding;
            if (actionFromName(key.substr(midiPrefix.size()), action) &&
                parseMidiBinding(value, binding)) {
                c.midi[action] = binding;
            }
            continue;
        }

        KeyBinding binding;
        if (actionFromName(key, action) && keyNameToBytes(value, binding)) {
            c.keys[action] = binding;
        }
    }
    return c;
}

bool saveConfig(const std::string& path, const Config& config) {
    std::ofstream f(path);
    if (!f) return false;
    if (config.input == InputMode::Midi) {
        f << kInputKey << kAssign << kMidiValue << kLineEnd;
        f << kMidiPortKey << kAssign << config.midiPort << kLineEnd;
        for (const auto& [action, binding] : config.midi) {
            f << kMidiKeyPrefix << actionName(action) << kAssign << formatMidiBinding(binding)
              << kLineEnd;
        }
    } else {
        f << kInputKey << kAssign << kKeyboardValue << kLineEnd;
        for (const auto& [action, binding] : config.keys) {
            f << actionName(action) << kAssign << binding.name << kLineEnd;
        }
    }
    f << kRampKey << kAssign << config.rampMs << kLineEnd;
    f << kBlinkKey << kAssign << config.blinkMs << kLineEnd;
    f.close();
    return !f.fail();
}

}  // namespace sampler
```

- [ ] **Step 5: Ver que pasa**

Run: `make test`
Expected: todos en verde. Los tests de config que ya existían siguen pasando sin tocarlos.

- [ ] **Step 6: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp
git commit -m "feat: input mode, midi bindings and saving in the configuration file"
```

---

### Task 3: `FdInput` e `InputMux`, con `main` leyendo del mux

**Files:**
- Create: `src/adapters/fd_input.h`
- Create: `src/adapters/input_mux.h`
- Create: `src/adapters/input_mux.cpp`
- Create: `tests/test_input_mux.cpp`
- Modify: `src/adapters/termios_keyboard.h` (reescritura completa, abajo)
- Modify: `src/adapters/termios_keyboard.cpp` (cambia `poll` por `fd`, `readBytes` y `readAction`)
- Modify: `src/main.cpp` (tres líneas)
- Modify: `Makefile` (`ADAPTER_SRCS` y `TEST_SRCS`)

**Interfaces:**
- Consumes: `sampler::InputSource` de `src/ports/input_source.h` (sin cambios), `matchAction` de `termios_keyboard.h` (sin cambios).
- Produces:
  - `class FdInput { virtual int fd() const = 0; virtual bool readAction(Action& outAction) = 0; };` en `src/adapters/fd_input.h`. `readAction` no bloquea nunca: se llama después de que `poll()` diga que hay datos.
  - `class InputMux : public InputSource { explicit InputMux(const std::vector<FdInput*>& inputs); bool poll(int timeoutMs, Action& outAction) override; };`
  - `TermiosKeyboard` implementa `FdInput` (ya no `InputSource`) y añade `bool readBytes(std::string& out);`, que lee los bytes crudos disponibles sin bloquear. La usará el asistente en la Task 7.

- [ ] **Step 1: Escribir los tests**

`tests/test_input_mux.cpp`:

```cpp
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
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp src/adapters/termios_keyboard.cpp src/adapters/ansi_display.cpp src/adapters/input_mux.cpp
```

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp tests/test_midi_binding.cpp tests/test_input_mux.cpp
```

- [ ] **Step 3: Ver que falla**

Run: `make test`
Expected: error de compilación, `'adapters/input_mux.h' file not found`.

- [ ] **Step 4: Escribir `src/adapters/fd_input.h`**

```cpp
#pragma once
#include "domain/action.h"

namespace sampler {

class FdInput {
public:
    virtual ~FdInput() = default;
    virtual int fd() const = 0;
    virtual bool readAction(Action& outAction) = 0;
};

}  // namespace sampler
```

- [ ] **Step 5: Escribir `src/adapters/input_mux.h` y `src/adapters/input_mux.cpp`**

`src/adapters/input_mux.h`:

```cpp
#pragma once
#include <poll.h>

#include <vector>

#include "adapters/fd_input.h"
#include "ports/input_source.h"

namespace sampler {

class InputMux : public InputSource {
public:
    explicit InputMux(const std::vector<FdInput*>& inputs);

    bool poll(int timeoutMs, Action& outAction) override;

private:
    std::vector<FdInput*> inputs_;
    std::vector<pollfd> fds_;
};

}  // namespace sampler
```

`src/adapters/input_mux.cpp`:

```cpp
#include "adapters/input_mux.h"

namespace sampler {

InputMux::InputMux(const std::vector<FdInput*>& inputs) : inputs_(inputs) {
    for (FdInput* input : inputs_) fds_.push_back(pollfd{input->fd(), POLLIN, 0});
}

bool InputMux::poll(int timeoutMs, Action& outAction) {
    if (::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeoutMs) <= 0) return false;
    for (size_t i = 0; i < fds_.size(); ++i) {
        if ((fds_[i].revents & POLLIN) && inputs_[i]->readAction(outAction)) return true;
    }
    return false;
}

}  // namespace sampler
```

- [ ] **Step 6: Pasar `TermiosKeyboard` a `FdInput`**

`src/adapters/termios_keyboard.h` queda así:

```cpp
#pragma once
#include <termios.h>

#include <cstddef>
#include <string>

#include "adapters/fd_input.h"
#include "config.h"

namespace sampler {

bool matchAction(const Config& config, const char* buf, size_t len, Action& outAction);

class TermiosKeyboard : public FdInput {
public:
    explicit TermiosKeyboard(const Config& config);
    ~TermiosKeyboard() override;

    bool enterRawMode();
    void restore();

    int fd() const override;
    bool readAction(Action& outAction) override;
    bool readBytes(std::string& out);

private:
    const Config& config_;
    termios original_{};
    bool raw_ = false;
};

}  // namespace sampler
```

En `src/adapters/termios_keyboard.cpp`, quita `#include <poll.h>` y sustituye la función `TermiosKeyboard::poll` entera por estas tres:

```cpp
int TermiosKeyboard::fd() const { return STDIN_FILENO; }

bool TermiosKeyboard::readBytes(std::string& out) {
    char buf[kReadBufferBytes];
    const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return false;
    out.assign(buf, static_cast<size_t>(n));
    return true;
}

bool TermiosKeyboard::readAction(Action& outAction) {
    std::string bytes;
    return readBytes(bytes) && matchAction(config_, bytes.data(), bytes.size(), outAction);
}
```

`matchAction`, el constructor, el destructor, `enterRawMode` y `restore` no cambian.

- [ ] **Step 7: Hacer que `main` lea del mux**

En `src/main.cpp`:

1. Añade `#include "adapters/input_mux.h"` justo antes de `#include "adapters/miniaudio_device.h"`.
2. Justo después de `sampler::TermiosKeyboard keyboard(config);` añade:

```cpp
    sampler::InputMux input({&keyboard});
```

3. Cambia `if (keyboard.poll(kPollTimeoutMs, action)) {` por:

```cpp
        if (input.poll(kPollTimeoutMs, action)) {
```

- [ ] **Step 8: Ver que pasa**

Run: `make test && make`
Expected: todos los tests en verde y `build/sampler` compila sin warnings.

- [ ] **Step 9: Comprobar a mano que el teclado sigue igual**

Si hay una carpeta de WAV a mano y un `sampler.conf` existente, arranca con ellos: flechas, espacio y `q` tienen que funcionar exactamente como antes. Si no, este camino lo cubre el punta a punta de teclado de la Task 7 (Step 7).

- [ ] **Step 10: Commit**

```bash
git add src/adapters/fd_input.h src/adapters/input_mux.h src/adapters/input_mux.cpp src/adapters/termios_keyboard.h src/adapters/termios_keyboard.cpp src/main.cpp tests/test_input_mux.cpp Makefile
git commit -m "feat(adapters): input mux polling several descriptors at once"
```

---

### Task 4: RtMidi en `third_party` y adaptador `RtMidiInput`

**Files:**
- Create: `third_party/RtMidi.h`, `third_party/RtMidi.cpp` (descargados)
- Create: `src/adapters/rtmidi_input.h`
- Create: `src/adapters/rtmidi_input.cpp`
- Create: `tests/test_rtmidi_input.cpp`
- Modify: `Makefile` (flags por sistema, regla de RtMidi, `ADAPTER_SRCS`, `TEST_SRCS`, objetos)

**Interfaces:**
- Consumes: `FdInput` (Task 3), `Config`, `InputMode` (Task 2), `matchMidi`, `samePort`, `stripPortSuffix`, `MidiBinding`, `MidiKind` (Task 1).
- Produces:
  - `class RtMidiInput : public FdInput { explicit RtMidiInput(const Config& config); bool open(); int fd() const override; bool readAction(Action& outAction) override; };`
  - `open()` busca `config.midiPort` con `samePort` y lo abre. Devuelve `false` si no lo encuentra o si RtMidi falla. Nunca lanza.
  - `config` tiene que vivir más que el `RtMidiInput`: el callback lo lee desde el hilo de MIDI.

- [ ] **Step 1: Descargar RtMidi 6.0.0**

```bash
curl -fsSL -o third_party/RtMidi.h https://raw.githubusercontent.com/thestk/rtmidi/6.0.0/RtMidi.h
curl -fsSL -o third_party/RtMidi.cpp https://raw.githubusercontent.com/thestk/rtmidi/6.0.0/RtMidi.cpp
shasum -a 256 third_party/RtMidi.h third_party/RtMidi.cpp
```

Expected, exactamente:

```
74f98ca172243b0b9c0c59f53c4e20285d879ffa86b3d61692109b013f4606f9  third_party/RtMidi.h
184d775e88e5d73b079897363fb42af155122e879f0bc9254a1c7edae4eadb3c  third_party/RtMidi.cpp
```

Si no coincide, para: la descarga no es la versión esperada.

- [ ] **Step 2: Enlazar RtMidi en el Makefile**

El bloque de sistema queda así:

```make
ifeq ($(UNAME),Darwin)
  LDLIBS := -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework CoreMIDI -lpthread -lm
  RTMIDI_API := __MACOSX_CORE__
else
  LDLIBS := -lpthread -lm -ldl -lasound
  RTMIDI_API := __LINUX_ALSA__
endif
```

Añade debajo de `TEST_SRCS`:

```make
VENDOR_SRCS := third_party/RtMidi.cpp
```

Y las dos líneas de objetos quedan así:

```make
APP_OBJS := $(addprefix $(BUILD)/,$(VENDOR_SRCS:.cpp=.o) $(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o) $(MAIN_SRC:.cpp=.o))
TEST_OBJS := $(addprefix $(BUILD)/,$(VENDOR_SRCS:.cpp=.o) $(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o) $(TEST_SRCS:.cpp=.o))
```

Añade esta regla justo debajo de la de `miniaudio_impl.o`:

```make
# RtMidi tampoco es cosa nuestra
$(BUILD)/third_party/RtMidi.o: third_party/RtMidi.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -w -MMD -MP -D$(RTMIDI_API) -Ithird_party -c -o $@ $<
```

Añade los ficheros nuevos:

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp src/adapters/termios_keyboard.cpp src/adapters/ansi_display.cpp src/adapters/input_mux.cpp src/adapters/rtmidi_input.cpp
```

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp tests/test_midi_binding.cpp tests/test_input_mux.cpp tests/test_rtmidi_input.cpp
```

- [ ] **Step 3: Escribir los tests**

Usan un puerto MIDI virtual que crea el propio test, así que no hace falta hardware. Si la máquina no tiene sistema MIDI (por ejemplo, un Linux sin `/dev/snd/seq`), el primer test se salta con un mensaje.

`tests/test_rtmidi_input.cpp`:

```cpp
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
```

- [ ] **Step 4: Ver que falla**

Run: `make test`
Expected: error de compilación, `'adapters/rtmidi_input.h' file not found`.

- [ ] **Step 5: Escribir `src/adapters/rtmidi_input.h`**

```cpp
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "adapters/fd_input.h"
#include "config.h"

class RtMidiIn;

namespace sampler {

constexpr int kClosedFd = -1;
constexpr size_t kPipeEnds = 2;

class RtMidiInput : public FdInput {
public:
    explicit RtMidiInput(const Config& config);
    ~RtMidiInput() override;

    bool open();

    int fd() const override;
    bool readAction(Action& outAction) override;

private:
    static void onMessage(double timeStamp, std::vector<unsigned char>* message, void* userData);

    const Config& config_;
    std::unique_ptr<RtMidiIn> in_;
    int pipe_[kPipeEnds] = {kClosedFd, kClosedFd};
};

}  // namespace sampler
```

- [ ] **Step 6: Escribir `src/adapters/rtmidi_input.cpp`**

```cpp
#include "adapters/rtmidi_input.h"

#include <fcntl.h>
#include <unistd.h>

#include "RtMidi.h"

namespace sampler {
namespace {
constexpr int kReadEnd = 0;
constexpr int kWriteEnd = 1;
}  // namespace

RtMidiInput::RtMidiInput(const Config& config) : config_(config) {}

RtMidiInput::~RtMidiInput() {
    in_.reset();
    for (int end : pipe_) {
        if (end != kClosedFd) ::close(end);
    }
}

bool RtMidiInput::open() {
    if (::pipe(pipe_) != 0) return false;
    ::fcntl(pipe_[kReadEnd], F_SETFL, O_NONBLOCK);
    ::fcntl(pipe_[kWriteEnd], F_SETFL, O_NONBLOCK);
    try {
        in_ = std::make_unique<RtMidiIn>();
        for (unsigned int i = 0; i < in_->getPortCount(); ++i) {
            if (!samePort(config_.midiPort, in_->getPortName(i))) continue;
            in_->setCallback(&RtMidiInput::onMessage, this);
            in_->openPort(i);
            return true;
        }
    } catch (const RtMidiError&) {
    }
    in_.reset();
    return false;
}

int RtMidiInput::fd() const { return pipe_[kReadEnd]; }

bool RtMidiInput::readAction(Action& outAction) {
    char byte;
    if (::read(pipe_[kReadEnd], &byte, sizeof(byte)) <= 0) return false;
    outAction = static_cast<Action>(byte);
    return true;
}

void RtMidiInput::onMessage(double, std::vector<unsigned char>* message, void* userData) {
    auto* self = static_cast<RtMidiInput*>(userData);
    Action action;
    if (!matchMidi(self->config_.midi, *message, action)) return;
    const char byte = static_cast<char>(action);
    // a full pipe drops the press rather than block the MIDI thread
    [[maybe_unused]] const ssize_t written = ::write(self->pipe_[kWriteEnd], &byte, sizeof(byte));
}

}  // namespace sampler
```

`in_.reset()` va antes de cerrar la tubería en el destructor: así el hilo de MIDI está parado antes de que desaparezca el descriptor en el que escribe.

- [ ] **Step 7: Ver que pasa**

Run: `make test && make`
Expected: todos en verde, sin warnings de nuestro código. En macOS el primer test usa CoreMIDI de verdad y no se salta.

- [ ] **Step 8: Commit**

```bash
git add third_party/RtMidi.h third_party/RtMidi.cpp src/adapters/rtmidi_input.h src/adapters/rtmidi_input.cpp tests/test_rtmidi_input.cpp Makefile
git commit -m "feat(adapters): rtmidi input delivering bound presses through a self-pipe"
```

---

### Task 5: `MidiScanner` para el asistente

**Files:**
- Create: `src/adapters/midi_scanner.h`
- Create: `src/adapters/midi_scanner.cpp`
- Create: `tests/test_midi_scanner.cpp`
- Modify: `Makefile` (`ADAPTER_SRCS` y `TEST_SRCS`)

**Interfaces:**
- Consumes: RtMidi (Task 4).
- Produces:
  - `struct MidiEvent { std::string port; std::vector<unsigned char> bytes; };`
  - `class MidiScanner { size_t open(); bool next(MidiEvent& out); };`
  - `open()` abre todos los puertos de entrada y devuelve cuántos ha podido abrir (0 si no hay ninguno o RtMidi falla). Nunca lanza.
  - `next()` no bloquea: devuelve el primer mensaje pendiente de cualquier puerto, con el nombre crudo del puerto (con sufijo de ALSA si lo hay).

- [ ] **Step 1: Escribir el test**

`tests/test_midi_scanner.cpp`:

```cpp
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
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp src/adapters/termios_keyboard.cpp src/adapters/ansi_display.cpp src/adapters/input_mux.cpp src/adapters/rtmidi_input.cpp src/adapters/midi_scanner.cpp
```

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp tests/test_midi_binding.cpp tests/test_input_mux.cpp tests/test_rtmidi_input.cpp tests/test_midi_scanner.cpp
```

- [ ] **Step 3: Ver que falla**

Run: `make test`
Expected: error de compilación, `'adapters/midi_scanner.h' file not found`.

- [ ] **Step 4: Escribir `src/adapters/midi_scanner.h`**

```cpp
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
```

- [ ] **Step 5: Escribir `src/adapters/midi_scanner.cpp`**

```cpp
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
```

- [ ] **Step 6: Ver que pasa**

Run: `make test`
Expected: todos en verde.

- [ ] **Step 7: Commit**

```bash
git add src/adapters/midi_scanner.h src/adapters/midi_scanner.cpp tests/test_midi_scanner.cpp Makefile
git commit -m "feat(adapters): midi scanner listening on every input port"
```

---

### Task 6: `SetupLearner`, la lógica pura del asistente

**Files:**
- Create: `src/setup_learner.h`
- Create: `src/setup_learner.cpp`
- Create: `tests/test_setup_learner.cpp`
- Modify: `Makefile` (`APP_SRCS` y `TEST_SRCS`)

**Interfaces:**
- Consumes: `Config`, `InputMode`, `defaultConfig`, `keyNameToBytes`, `keyNameFromBytes` (Task 2), `MidiBinding`, `midiBindingFromMessage`, `stripPortSuffix` (Task 1).
- Produces:
  - `enum class LearnResult { Accepted, Duplicate, Unsupported, Ignored };`
  - `constexpr Action kLearnOrder[] = {Action::Next, Action::Prev, Action::Toggle, Action::Quit};`
  - `class SetupLearner { explicit SetupLearner(InputMode mode); bool done() const; Action current() const; Action conflict() const; LearnResult offerKey(const std::string& bytes); LearnResult offerMidi(const std::string& port, const std::vector<unsigned char>& message); Config result() const; };`
  - `current()` solo es válido mientras `!done()`. `conflict()` es la acción que ya usaba lo pulsado, válida tras un `Duplicate`.
  - En MIDI, un mensaje de otro aparato distinto del fijado devuelve `Ignored`.
  - `result()` parte de `defaultConfig()`, así que en modo MIDI las teclas por defecto siguen ahí como respaldo.

- [ ] **Step 1: Escribir los tests**

`tests/test_setup_learner.cpp`:

```cpp
#include "doctest.h"
#include "setup_learner.h"

using sampler::Action;
using sampler::InputMode;
using sampler::LearnResult;
using sampler::SetupLearner;

namespace {
constexpr const char* kNano = "nano:nano MIDI 1 20:0";
constexpr const char* kPad = "Launchpad:Launchpad MIDI 1 24:0";
}

TEST_CASE("keys are asked in the order next, prev, toggle, quit") {
    SetupLearner learner(InputMode::Keyboard);
    CHECK(learner.current() == Action::Next);
    CHECK(learner.offerKey("\x1b[C") == LearnResult::Accepted);
    CHECK(learner.current() == Action::Prev);
    CHECK(learner.offerKey("\x1b[D") == LearnResult::Accepted);
    CHECK(learner.current() == Action::Toggle);
    CHECK(learner.offerKey(" ") == LearnResult::Accepted);
    CHECK(learner.current() == Action::Quit);
    CHECK_FALSE(learner.done());
    CHECK(learner.offerKey("x") == LearnResult::Accepted);
    CHECK(learner.done());

    auto c = learner.result();
    CHECK(c.input == InputMode::Keyboard);
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Prev).name == "left");
    CHECK(c.keys.at(Action::Toggle).name == "space");
    CHECK(c.keys.at(Action::Quit).bytes == "x");
}

TEST_CASE("a key already taken is rejected and the same action is asked again") {
    SetupLearner learner(InputMode::Keyboard);
    learner.offerKey("a");
    CHECK(learner.offerKey("a") == LearnResult::Duplicate);
    CHECK(learner.conflict() == Action::Next);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("both sequences of an arrow count as the same key") {
    SetupLearner learner(InputMode::Keyboard);
    learner.offerKey("\x1b[C");
    CHECK(learner.offerKey("\x1bOC") == LearnResult::Duplicate);
}

TEST_CASE("an unnamed key is unsupported and does not advance") {
    SetupLearner learner(InputMode::Keyboard);
    CHECK(learner.offerKey("\x1bOP") == LearnResult::Unsupported);
    CHECK(learner.current() == Action::Next);
}

TEST_CASE("the first midi press picks the device and other devices are ignored") {
    SetupLearner learner(InputMode::Midi);
    CHECK(learner.offerMidi(kNano, {0xB0, 44, 127}) == LearnResult::Accepted);
    CHECK(learner.offerMidi(kPad, {0x90, 60, 100}) == LearnResult::Ignored);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("midi releases do not advance") {
    SetupLearner learner(InputMode::Midi);
    CHECK(learner.offerMidi(kNano, {0x90, 60, 100}) == LearnResult::Accepted);
    CHECK(learner.offerMidi(kNano, {0x80, 60, 0}) == LearnResult::Ignored);
    CHECK(learner.offerMidi(kNano, {0x90, 60, 0}) == LearnResult::Ignored);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("a midi control already taken is rejected") {
    SetupLearner learner(InputMode::Midi);
    learner.offerMidi(kNano, {0xB0, 44, 127});
    CHECK(learner.offerMidi(kNano, {0xB0, 44, 64}) == LearnResult::Duplicate);
    CHECK(learner.conflict() == Action::Next);
    CHECK(learner.current() == Action::Prev);
}

TEST_CASE("a finished midi setup keeps the device without the alsa suffix and the backup keys") {
    SetupLearner learner(InputMode::Midi);
    learner.offerMidi(kNano, {0xB0, 44, 127});
    learner.offerMidi(kNano, {0xB0, 43, 127});
    learner.offerMidi(kNano, {0x90, 60, 100});
    learner.offerMidi(kNano, {0xB0, 46, 127});
    REQUIRE(learner.done());

    auto c = learner.result();
    const sampler::MidiBinding toggle{sampler::MidiKind::Note, 1, 60};
    CHECK(c.input == InputMode::Midi);
    CHECK(c.midiPort == "nano:nano MIDI 1");
    CHECK(c.midi.size() == 4);
    CHECK(c.midi.at(Action::Toggle) == toggle);
    CHECK(c.keys.at(Action::Next).name == "right");
}
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

```make
APP_SRCS := src/config.cpp src/midi_binding.cpp src/setup_learner.cpp
```

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp tests/test_midi_binding.cpp tests/test_input_mux.cpp tests/test_rtmidi_input.cpp tests/test_midi_scanner.cpp tests/test_setup_learner.cpp
```

- [ ] **Step 3: Ver que falla**

Run: `make test`
Expected: error de compilación, `'setup_learner.h' file not found`.

- [ ] **Step 4: Escribir `src/setup_learner.h`**

```cpp
#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "config.h"

namespace sampler {

enum class LearnResult { Accepted, Duplicate, Unsupported, Ignored };

constexpr Action kLearnOrder[] = {Action::Next, Action::Prev, Action::Toggle, Action::Quit};

class SetupLearner {
public:
    explicit SetupLearner(InputMode mode);

    bool done() const;
    Action current() const;
    Action conflict() const;

    LearnResult offerKey(const std::string& bytes);
    LearnResult offerMidi(const std::string& port, const std::vector<unsigned char>& message);

    Config result() const;

private:
    InputMode mode_;
    size_t step_ = 0;
    Action conflict_ = Action::Next;
    std::string port_;
    std::map<Action, std::string> keys_;
    std::map<Action, MidiBinding> midi_;
};

}  // namespace sampler
```

- [ ] **Step 5: Escribir `src/setup_learner.cpp`**

```cpp
#include "setup_learner.h"

#include <iterator>

namespace sampler {
namespace {

template <typename T>
bool taken(const std::map<Action, T>& learned, const T& value, Action& owner) {
    for (const auto& [action, known] : learned) {
        if (known == value) {
            owner = action;
            return true;
        }
    }
    return false;
}

}  // namespace

SetupLearner::SetupLearner(InputMode mode) : mode_(mode) {}

bool SetupLearner::done() const { return step_ == std::size(kLearnOrder); }

Action SetupLearner::current() const { return kLearnOrder[step_]; }

Action SetupLearner::conflict() const { return conflict_; }

LearnResult SetupLearner::offerKey(const std::string& bytes) {
    std::string name;
    if (!keyNameFromBytes(bytes, name)) return LearnResult::Unsupported;
    if (taken(keys_, name, conflict_)) return LearnResult::Duplicate;
    keys_[current()] = name;
    ++step_;
    return LearnResult::Accepted;
}

LearnResult SetupLearner::offerMidi(const std::string& port,
                                    const std::vector<unsigned char>& message) {
    MidiBinding binding;
    if (!midiBindingFromMessage(message, binding)) return LearnResult::Ignored;
    if (!port_.empty() && port != port_) return LearnResult::Ignored;
    if (taken(midi_, binding, conflict_)) return LearnResult::Duplicate;
    port_ = port;
    midi_[current()] = binding;
    ++step_;
    return LearnResult::Accepted;
}

Config SetupLearner::result() const {
    Config config = defaultConfig();
    config.input = mode_;
    config.midiPort = stripPortSuffix(port_);
    config.midi = midi_;
    for (const auto& [action, name] : keys_) keyNameToBytes(name, config.keys[action]);
    return config;
}

}  // namespace sampler
```

- [ ] **Step 6: Ver que pasa**

Run: `make test`
Expected: todos en verde.

- [ ] **Step 7: Commit**

```bash
git add src/setup_learner.h src/setup_learner.cpp tests/test_setup_learner.cpp Makefile
git commit -m "feat: setup learner with ordering, duplicates and device locking"
```

---

### Task 7: Asistente interactivo, `main` con MIDI y documentación

**Files:**
- Create: `src/setup_wizard.h`
- Create: `src/setup_wizard.cpp`
- Modify: `src/main.cpp` (reescritura completa, abajo)
- Modify: `Makefile` (`APP_SRCS`)
- Modify: `README.md`, `prod_checklist.md`, `sampler.conf.example`

**Interfaces:**
- Consumes: `TermiosKeyboard` con `fd()`, `readBytes()`, `enterRawMode()` (Task 3), `InputMux` (Task 3), `RtMidiInput` (Task 4), `MidiScanner`, `MidiEvent` (Task 5), `SetupLearner`, `LearnResult` (Task 6), `saveConfig` (Task 2).
- Produces:
  - `enum class WizardOutcome { Saved, Unsaved, Cancelled, NoTerminal };`
  - `WizardOutcome runSetupWizard(const std::string& path, const std::atomic<bool>& cancel, Config& out);`
  - `out` solo es válido si el resultado es `Saved` o `Unsaved`.

Este bucle no tiene tests unitarios, igual que `main`. Se verifica de punta a punta con `expect` en los pasos 6 a 9.

- [ ] **Step 1: Escribir `src/setup_wizard.h`**

```cpp
#pragma once
#include <atomic>
#include <string>

#include "config.h"

namespace sampler {

enum class WizardOutcome { Saved, Unsaved, Cancelled, NoTerminal };

WizardOutcome runSetupWizard(const std::string& path, const std::atomic<bool>& cancel,
                             Config& out);

}  // namespace sampler
```

- [ ] **Step 2: Escribir `src/setup_wizard.cpp`**

```cpp
#include "setup_wizard.h"

#include <poll.h>

#include <chrono>
#include <cstdio>
#include <thread>

#include "adapters/midi_scanner.h"
#include "adapters/termios_keyboard.h"
#include "setup_learner.h"

namespace sampler {
namespace {

constexpr int kWizardPollMs = 20;
constexpr const char* kKeyboardChoice = "t";
constexpr const char* kMidiChoice = "m";

const char* actionLabel(Action action) {
    switch (action) {
        case Action::Next:   return "SIGUIENTE";
        case Action::Prev:   return "ANTERIOR";
        case Action::Toggle: return "PLAY/STOP";
        case Action::Quit:   return "SALIR";
    }
    return "";
}

bool waitBytes(TermiosKeyboard& keyboard, const std::atomic<bool>& cancel, std::string& out) {
    while (!cancel.load()) {
        pollfd fd{keyboard.fd(), POLLIN, 0};
        if (::poll(&fd, 1, kWizardPollMs) > 0 && keyboard.readBytes(out)) return true;
    }
    return false;
}

bool waitMidi(MidiScanner& scanner, const std::atomic<bool>& cancel, MidiEvent& out) {
    while (!cancel.load()) {
        if (scanner.next(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kWizardPollMs));
    }
    return false;
}

void prompt(const SetupLearner& learner) {
    std::printf("pulsa para %s\n", actionLabel(learner.current()));
}

void report(LearnResult result, const SetupLearner& learner) {
    if (result == LearnResult::Duplicate) {
        std::printf("ya esta usado para %s, prueba otro\n", actionLabel(learner.conflict()));
    }
    if (result == LearnResult::Unsupported) std::printf("tecla no soportada, prueba otra\n");
    if (result == LearnResult::Accepted && !learner.done()) prompt(learner);
}

bool learnKeys(TermiosKeyboard& keyboard, const std::atomic<bool>& cancel,
               SetupLearner& learner) {
    prompt(learner);
    std::string bytes;
    while (!learner.done()) {
        if (!waitBytes(keyboard, cancel, bytes)) return false;
        report(learner.offerKey(bytes), learner);
    }
    return true;
}

bool learnMidi(MidiScanner& scanner, const std::atomic<bool>& cancel, SetupLearner& learner) {
    prompt(learner);
    MidiEvent event;
    while (!learner.done()) {
        if (!waitMidi(scanner, cancel, event)) return false;
        report(learner.offerMidi(event.port, event.bytes), learner);
    }
    return true;
}

}  // namespace

WizardOutcome runSetupWizard(const std::string& path, const std::atomic<bool>& cancel,
                             Config& out) {
    const Config defaults = defaultConfig();
    TermiosKeyboard keyboard(defaults);
    if (!keyboard.enterRawMode()) return WizardOutcome::NoTerminal;

    std::printf("no existe %s, vamos a crearlo\n", path.c_str());
    for (;;) {
        std::printf("entrada? [t] teclado / [m] midi\n");
        std::string choice;
        if (!waitBytes(keyboard, cancel, choice)) return WizardOutcome::Cancelled;

        if (choice == kKeyboardChoice) {
            SetupLearner learner(InputMode::Keyboard);
            if (!learnKeys(keyboard, cancel, learner)) return WizardOutcome::Cancelled;
            out = learner.result();
            break;
        }
        if (choice == kMidiChoice) {
            MidiScanner scanner;
            if (scanner.open() == 0) {
                std::printf("no encuentro ningun midi conectado. conectalo y vuelve a arrancar, o usa el teclado\n");
                continue;
            }
            SetupLearner learner(InputMode::Midi);
            if (!learnMidi(scanner, cancel, learner)) return WizardOutcome::Cancelled;
            out = learner.result();
            break;
        }
    }

    if (!saveConfig(path, out)) {
        std::printf("no se pudo guardar %s, sigo sin guardarlo\n", path.c_str());
        return WizardOutcome::Unsaved;
    }
    std::printf("guardado en %s\n", path.c_str());
    return WizardOutcome::Saved;
}

}  // namespace sampler
```

El mensaje de "no encuentro ningun midi" pide reiniciar porque en macOS, sin run loop, CoreMIDI puede no ver un aparato enchufado después de que arranque el proceso.

- [ ] **Step 3: Añadir el asistente al Makefile**

```make
APP_SRCS := src/config.cpp src/midi_binding.cpp src/setup_learner.cpp src/setup_wizard.cpp
```

- [ ] **Step 4: Reescribir `src/main.cpp`**

Cambios respecto a hoy: el config sale del asistente si el fichero no existe, el MIDI se abre después de cargar los WAV (avisando si no está) y el mux junta teclado y MIDI. El handler de `SIGINT` solo se instala durante el asistente y se quita al acabar, para que los avisos de antes de la pantalla sigan saliendo con Ctrl-C como hasta ahora.

```cpp
#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "adapters/ansi_display.h"
#include "adapters/input_mux.h"
#include "adapters/miniaudio_device.h"
#include "adapters/rtmidi_input.h"
#include "adapters/termios_keyboard.h"
#include "adapters/wav_folder_repository.h"
#include "config.h"
#include "domain/transport.h"
#include "setup_wizard.h"

namespace {

constexpr int kPollTimeoutMs = 50;
constexpr uint32_t kMsPerSecond = 1000;

std::atomic<bool> g_quit{false};

void onSignal(int) { g_quit.store(true); }

uint64_t nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kMsPerSecond +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <carpeta-de-samples> [sampler.conf]\n", argv[0]);
        return 1;
    }
    const std::string folder = argv[1];
    const std::string configPath = argc > 2 ? argv[2] : "sampler.conf";

    sampler::Config config;
    std::error_code ec;
    if (std::filesystem::exists(configPath, ec)) {
        config = sampler::loadConfig(configPath);
    } else {
        std::signal(SIGINT, onSignal);
        const sampler::WizardOutcome outcome = sampler::runSetupWizard(configPath, g_quit, config);
        std::signal(SIGINT, SIG_DFL);
        if (outcome == sampler::WizardOutcome::NoTerminal) {
            std::fprintf(stderr, "stdin no es un terminal\n");
            return 1;
        }
        if (outcome == sampler::WizardOutcome::Cancelled) return 1;
    }

    sampler::MiniaudioDevice device;
    if (!device.open()) {
        std::fprintf(stderr, "no se pudo abrir el dispositivo de audio\n");
        return 1;
    }

    sampler::WavFolderRepository repo;
    sampler::LoadResult loaded = repo.load(folder, device.sampleRate());

    if (!loaded.failed.empty()) {
        std::fprintf(stderr, "no se pudieron cargar %zu archivos:\n", loaded.failed.size());
        for (const auto& f : loaded.failed) std::fprintf(stderr, "  %s\n", f.c_str());
        std::fprintf(stderr, "\nindices finales:\n");
        for (size_t i = 0; i < loaded.library.samples.size(); ++i) {
            std::fprintf(stderr, "  %zu  %s\n", i, loaded.library.samples[i].name.c_str());
        }
        std::fprintf(stderr, "\npulsa intro para continuar, ctrl-c para salir\n");
        std::getchar();
    }

    if (loaded.library.samples.empty()) {
        std::fprintf(stderr, "no hay ningun .wav utilizable en %s\n", folder.c_str());
        return 1;
    }

    sampler::RtMidiInput midi(config);
    const bool wantsMidi = config.input == sampler::InputMode::Midi;
    const bool midiReady = wantsMidi && midi.open();
    if (wantsMidi && !midiReady) {
        std::fprintf(stderr, "no encuentro el midi \"%s\"\n", config.midiPort.c_str());
        std::fprintf(stderr, "pulsa intro para seguir solo con teclado, ctrl-c para salir\n");
        std::getchar();
    }

    const uint32_t rampFrames = config.rampMs * device.sampleRate() / kMsPerSecond;
    sampler::Transport transport(static_cast<int>(loaded.library.samples.size()), rampFrames);

    device.bind(&transport, &loaded.library);
    if (!device.start()) {
        std::fprintf(stderr, "no se pudo arrancar el dispositivo de audio\n");
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    sampler::TermiosKeyboard keyboard(config);
    std::vector<sampler::FdInput*> sources{&keyboard};
    if (midiReady) sources.push_back(&midi);
    sampler::InputMux input(sources);
    sampler::AnsiDisplay display;
    if (!keyboard.enterRawMode()) {
        std::fprintf(stderr, "stdin no es un terminal\n");
        return 1;
    }
    display.enter();

    bool blinkPhase = true;
    uint64_t lastBlink = nowMs();
    int lastDrawnIndex = -1;
    bool lastDrawnVisible = false;

    while (!g_quit.load()) {
        sampler::Action action;
        if (input.poll(kPollTimeoutMs, action)) {
            switch (action) {
                case sampler::Action::Prev:   transport.step(-1); break;
                case sampler::Action::Next:   transport.step(+1); break;
                case sampler::Action::Toggle: transport.toggle(); break;
                case sampler::Action::Quit:   g_quit.store(true); break;
            }
        }

        const bool playing = transport.isPlaying();
        const uint64_t now = nowMs();
        if (playing && now - lastBlink >= config.blinkMs) {
            blinkPhase = !blinkPhase;
            lastBlink = now;
        }
        if (!playing) {
            blinkPhase = true;
            lastBlink = now;
        }

        const int index = transport.armed();
        if (index != lastDrawnIndex || blinkPhase != lastDrawnVisible) {
            display.draw(index, blinkPhase);
            lastDrawnIndex = index;
            lastDrawnVisible = blinkPhase;
        }
    }

    display.leave();
    keyboard.restore();
    device.stop();
    return 0;
}
```

- [ ] **Step 5: Compilar y pasar los tests**

Run: `make test && make`
Expected: todos en verde y `build/sampler` compila sin warnings.

- [ ] **Step 6: Preparar el banco de pruebas de punta a punta**

Todo en el scratchpad, nada de esto se commitea. `$S` es el directorio scratchpad de la sesión.

```bash
S=<scratchpad>/e2e && mkdir -p $S
python3 - "$S/samples" <<'EOF'
import math, os, struct, sys, wave
d = sys.argv[1]
os.makedirs(d, exist_ok=True)
for i, f in enumerate((220, 330)):
    with wave.open(os.path.join(d, f"0{i + 1}_tone.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * f * n / 48000))) for n in range(48000)))
EOF
cat > $S/midi_fake.cpp <<'EOF'
#include <cstdio>
#include <vector>
#include "RtMidi.h"
int main() {
    RtMidiOut out;
    out.openVirtualPort("sampler-fake");
    std::printf("ready\n");
    std::fflush(stdout);
    unsigned status, number, value;
    while (std::scanf("%x %u %u", &status, &number, &value) == 3) {
        std::vector<unsigned char> m{(unsigned char)status, (unsigned char)number, (unsigned char)value};
        out.sendMessage(&m);
    }
}
EOF
c++ -std=c++17 -w -Ithird_party $S/midi_fake.cpp build/third_party/RtMidi.o -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -o $S/midi_fake
```

En Linux, cambia los `-framework` por `-lasound -lpthread`.

- [ ] **Step 7: De punta a punta, asistente de teclado**

```bash
rm -f $S/keys.conf
expect <<EOF
set timeout 5
spawn ./build/sampler $S/samples $S/keys.conf
expect "entrada?"
send "t"
expect "pulsa para SIGUIENTE"
send "d"
expect "pulsa para ANTERIOR"
send "d"
expect "ya esta usado para SIGUIENTE"
send "a"
expect "pulsa para PLAY/STOP"
send " "
expect "pulsa para SALIR"
send "x"
expect "guardado en"
sleep 1
send "x"
expect eof
EOF
cat $S/keys.conf
```

Expected: `expect` termina sin timeout y el fichero contiene, en este orden: `input = keyboard`, `prev = a`, `next = d`, `toggle = space`, `quit = x`, `ramp_ms = 2`, `blink_ms = 250`.

- [ ] **Step 8: De punta a punta, asistente MIDI y salida por MIDI**

```bash
rm -f $S/midi.conf
expect <<EOF
set timeout 5
spawn $S/midi_fake
set fake \$spawn_id
expect -i \$fake "ready"
spawn ./build/sampler $S/samples $S/midi.conf
set app \$spawn_id
expect -i \$app "entrada?"
send -i \$app "m"
expect -i \$app "pulsa para SIGUIENTE"
send -i \$fake "b0 44 127\r"
expect -i \$app "pulsa para ANTERIOR"
send -i \$fake "b0 44 0\r"
send -i \$fake "b0 43 127\r"
expect -i \$app "pulsa para PLAY/STOP"
send -i \$fake "90 60 100\r"
expect -i \$app "pulsa para SALIR"
send -i \$fake "b0 46 127\r"
expect -i \$app "guardado en"
sleep 1
send -i \$fake "b0 46 127\r"
expect -i \$app eof
EOF
cat $S/midi.conf
```

Expected: `expect` termina sin timeout. Eso demuestra que el asistente aprende, que el `0` de soltar no avanza, y que en el arranque normal `RtMidiInput` encuentra el puerto por nombre y la acción SALIR llega por el self-pipe. El fichero contiene `input = midi`, `midi_port = sampler-fake` (en Linux, el nombre sin el sufijo numérico), `midi_prev = cc:1:43`, `midi_next = cc:1:44`, `midi_toggle = note:1:60`, `midi_quit = cc:1:46`, `ramp_ms = 2` y `blink_ms = 250`.

- [ ] **Step 9: De punta a punta, Ctrl-C, sin terminal y MIDI ausente**

```bash
rm -f $S/cancel.conf
expect <<EOF
set timeout 5
spawn ./build/sampler $S/samples $S/cancel.conf
expect "entrada?"
send "t"
expect "pulsa para SIGUIENTE"
send "\003"
expect eof
EOF
test ! -e $S/cancel.conf && echo "cancel OK"

rm -f $S/nada.conf
./build/sampler $S/samples $S/nada.conf < /dev/null; echo "exit=$?"

printf 'input = midi\nmidi_port = no existe\n' > $S/absent.conf
echo | ./build/sampler $S/samples $S/absent.conf; echo "exit=$?"
```

Expected:
- `cancel OK` (Ctrl-C no deja fichero).
- `stdin no es un terminal` y `exit=1`.
- `no encuentro el midi "no existe"`, luego `pulsa intro para seguir solo con teclado...`, y después `stdin no es un terminal` con `exit=1`, porque stdin es una tubería. Lo que importa es que el aviso sale y no hay crash.

- [ ] **Step 10: Actualizar `sampler.conf.example`**

Añade al principio, antes de `prev = left`:

```
# entrada: keyboard o midi
input = keyboard

# solo con input = midi. formato tipo:canal:numero, tipo note o cc
# midi_port   = nanoKONTROL2
# midi_next   = cc:1:44
# midi_prev   = cc:1:43
# midi_toggle = note:1:60
# midi_quit   = cc:1:46

```

- [ ] **Step 11: Actualizar `README.md`**

La primera frase pasa a ser:

```
Reproductor de loops por teclado o MIDI para tocar en directo. Carga una carpeta de
```

Y la sección `## Usar` queda así:

```markdown
## Usar

    ./build/sampler ./mis_samples [sampler.conf]

La primera vez, si no existe el config, el programa pregunta si vas a usar
teclado o MIDI y te pide pulsar lo que quieras para cada acción: siguiente,
anterior, play/stop y salir. Lo guarda en `sampler.conf`, o en la ruta que
pases. Para repetirlo, borra el fichero.

Con MIDI vale cualquier botón o pad que mande Note On o Control Change. Si un
botón está en modo toggle (manda 127 al pulsar y 0 al volver a pulsar), se
pierde una de cada dos pulsaciones: ponlo en modo momentáneo. El teclado sigue
funcionando con sus teclas por defecto como respaldo. Si al arrancar no
encuentra el controlador, avisa y sigue solo con teclado.

Con las teclas por defecto:

- **Flechas**: mueven el índice. Si está parado, en silencio. Si está sonando,
  arman el destino y el cambio entra al acabar la vuelta. Las pulsaciones se
  acumulan.
- **Espacio**: arranca desde el principio, o para y resetea.
- **q** o **Ctrl-C**: salir.
```

El resto del README no cambia.

- [ ] **Step 12: Actualizar `prod_checklist.md`**

En las dos secciones (macOS y Linux), añade justo antes de la línea `- [ ] Arranca desde una ventana de terminal: ...`:

```markdown
- [ ] Si usas MIDI, conecta el controlador antes de arrancar. No se reconecta en caliente.
- [ ] Si es la primera vez en esta máquina, el asistente te pedirá las cuatro acciones. Para repetirlo, borra `sampler.conf`.
```

Y al final de cada sección:

```markdown
- [ ] Si usas MIDI, pulsa SIGUIENTE en el controlador y confirma que cambia el número.
```

- [ ] **Step 13: Commit**

```bash
git add src/setup_wizard.h src/setup_wizard.cpp src/main.cpp Makefile README.md prod_checklist.md sampler.conf.example
git commit -m "feat: first-run setup wizard and midi input wired into the sampler"
```
