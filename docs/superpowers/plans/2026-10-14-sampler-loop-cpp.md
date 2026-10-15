# Sampler de loops por teclado — plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un ejecutable de consola que carga una carpeta de WAV en RAM y permite encadenar loops con tres teclas, cambiando de sample siempre en el límite de la vuelta y sin interrumpir el audio.

**Architecture:** Hexagonal. El dominio (`Transport`) es una máquina de estados pura y RT-safe que rellena un buffer de floats; no conoce audio, teclado ni terminal. Dos hilos sin locks: el de audio es dueño de la posición de reproducción, el principal escribe dos atómicos (`armedIndex`, `playRequested`) que el de audio lee. Los adaptadores (miniaudio, termios, ANSI) viven detrás de puertos y son sustituibles.

**Tech Stack:** C++17, miniaudio (header-only, audio y decodificación de WAV), doctest (header-only, tests), Makefile con `uname -s`. Sin dependencias instaladas en el sistema.

**Spec:** `docs/superpowers/specs/2026-10-14-sampler-loop-cpp-design.md`

## Global Constraints

- C++17. Compilador: `clang++` en macOS, `g++` o `clang++` en Linux.
- Cero dependencias instaladas en el sistema. Todo lo externo vive en `third_party/` como header.
- Plataformas soportadas: macOS y Linux. Windows está fuera de alcance.
- Formato interno de audio invariable: **float32, 2 canales intercalados, a la frecuencia del dispositivo**. La constante es `kChannels = 2`.
- Formato de entrada: **solo `.wav`**.
- `Transport::render` es código de tiempo real: prohibido reservar o liberar memoria, tomar locks, tocar disco, lanzar excepciones o llamar a nada de la stdlib que haga cualquiera de esas cosas.
- `SampleLibrary` es de solo lectura una vez cargada y antes de arrancar el dispositivo.
- Nada de literales numéricos sueltos en código de producción: constantes con nombre en el header que corresponda. En los tests sí se permiten literales.
- Comentarios: por defecto ninguno. Como mucho una línea corta explicando un "por qué" no evidente, y nunca más de tres líneas de comentario en un mismo bloque.

## Review Focus

Cinco cosas que el spec da por supuestas y que romperían el programa en manos de alguien que lo use de verdad. Cada una tiene su test en la tarea que posee el código.

1. **Carpeta con un solo sample.** `step(+1)` con `n == 1` tiene que dejar el índice en 0, no en 1. El módulo lo resuelve, pero hay que pinarlo. (Task 3)
2. **Librería vacía (`n == 0`).** `render` no puede indexar ni dividir: tiene que emitir silencio y salir. `main` aborta antes de llegar aquí, pero el dominio no puede confiar en eso. (Task 2)
3. **Sample más corto que la rampa.** Con `frameCount <= rampFrames` el cálculo de `fadeOutStart` desbordaría en aritmética sin signo. Hay que saturar a 0 y que la ganancia siga acotada en [0, 1]. (Task 4)
4. **WAV válido pero de 0 frames.** Un archivo así haría que `render` entrara en un bucle infinito en el límite de vuelta. Se rechaza en la carga, no en el dominio. (Task 5)
5. **Terminal más estrecha que el texto a pintar.** El cálculo del centro no puede dar una columna negativa ni escribir fuera del ancho. (Task 9)

---

### Task 1: Esqueleto del proyecto y build

Deja el repo compilando y con `make test` verde contra un test trivial. Todo lo demás se apoya en esto.

**Files:**
- Create: `third_party/doctest.h` (descargado)
- Create: `third_party/miniaudio.h` (descargado)
- Create: `src/adapters/miniaudio_impl.cpp`
- Create: `tests/test_main.cpp`
- Create: `Makefile`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nada.
- Produces: los objetivos `make`, `make test`, `make clean`. La variable `TEST_SRCS` del Makefile, a la que las tareas siguientes añaden ficheros de test.

- [ ] **Step 1: Descargar las dos cabeceras de terceros**

```bash
mkdir -p third_party src/domain src/ports src/adapters tests
curl -fsSL -o third_party/doctest.h \
  https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
curl -fsSL -o third_party/miniaudio.h \
  https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
```

- [ ] **Step 2: Verificar que las descargas son correctas**

```bash
grep -q "define DOCTEST_VERSION_STR" third_party/doctest.h && echo "doctest OK"
grep -q "ma_device_init" third_party/miniaudio.h && echo "miniaudio OK"
```

Esperado: las dos líneas `OK`. Si alguna falla, la descarga trajo una página de error en vez del header: compruébalo con `head -5` antes de seguir.

- [ ] **Step 3: Aislar la implementación de miniaudio en su propia unidad de traducción**

`src/adapters/miniaudio_impl.cpp`:

```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
```

- [ ] **Step 4: Escribir el test trivial**

`tests/test_main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("the test harness runs") {
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 5: Escribir el Makefile**

```make
UNAME := $(shell uname -s)
CXX ?= clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Ithird_party -Isrc
BUILD := build

ifeq ($(UNAME),Darwin)
  LDLIBS := -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -lpthread -lm
else
  LDLIBS := -lpthread -lm -ldl
endif

DOMAIN_SRCS :=
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp
APP_SRCS :=
TEST_SRCS := tests/test_main.cpp

APP_OBJS := $(addprefix $(BUILD)/,$(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o))
TEST_OBJS := $(addprefix $(BUILD)/,$(DOMAIN_SRCS:.cpp=.o) $(TEST_SRCS:.cpp=.o))

all: $(BUILD)/sampler

$(BUILD)/sampler: $(APP_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/tests: $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# miniaudio genera warnings propios que no son cosa nuestra
$(BUILD)/src/adapters/miniaudio_impl.o: src/adapters/miniaudio_impl.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -w -Ithird_party -c -o $@ $<

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(BUILD)/tests
	./$(BUILD)/tests

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
```

Nota: cuando `APP_SRCS` esté vacío el objetivo `all` enlazará solo miniaudio y fallará por falta de `main`. Es esperado hasta la Task 10; hasta entonces usa `make test`.

- [ ] **Step 6: Ejecutar los tests**

Run: `make test`
Esperado: compila y `doctest` imprime `1 test case, 1 assertion, 0 failed`.

- [ ] **Step 7: Commit**

```bash
git add third_party Makefile src/adapters/miniaudio_impl.cpp tests/test_main.cpp .gitignore
git commit -m "build: project skeleton with miniaudio, doctest and Makefile"
```

---

### Task 2: El dominio, play y loop

El tipo `Sample`, la `SampleLibrary` y la parte del `Transport` que arranca, para y repite un sample. Sin cambio de sample y sin rampas todavía.

**Files:**
- Create: `src/domain/sample.h`
- Create: `src/domain/action.h`
- Create: `src/domain/transport.h`
- Create: `src/domain/transport.cpp`
- Create: `tests/support/fake_library.h`
- Create: `tests/test_transport_playback.cpp`
- Modify: `Makefile` (`DOMAIN_SRCS`, `TEST_SRCS`)

**Interfaces:**
- Consumes: el build de la Task 1.
- Produces:
  - `constexpr uint32_t kChannels = 2;`
  - `struct Sample { std::vector<float> frames; uint64_t frameCount; std::string name; };`
  - `struct SampleLibrary { std::vector<Sample> samples; uint32_t sampleRate; };`
  - `enum class Action { Prev, Next, Toggle, Quit };`
  - `class Transport` con `Transport(int sampleCount, uint32_t rampFrames)`, `void step(int delta)`, `void toggle()`, `bool isPlaying() const`, `int armed() const`, `void render(float* out, uint32_t frames, const SampleLibrary& lib)`.
  - `tests/support/fake_library.h` con `SampleLibrary makeLib(std::vector<uint64_t> lengths)`.

- [ ] **Step 1: Escribir los tipos del dominio**

`src/domain/sample.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sampler {

constexpr uint32_t kChannels = 2;

struct Sample {
    std::vector<float> frames;
    uint64_t frameCount = 0;
    std::string name;
};

struct SampleLibrary {
    std::vector<Sample> samples;
    uint32_t sampleRate = 0;
};

}  // namespace sampler
```

`src/domain/action.h`:

```cpp
#pragma once

namespace sampler {

enum class Action { Prev, Next, Toggle, Quit };

}  // namespace sampler
```

- [ ] **Step 2: Escribir el helper de tests**

El valor de cada muestra codifica quién suena y en qué frame: el sample `i` vale `0.1*(i+1)` y cada frame `j` suma `j/1000`. Así un solo `CHECK` sobre una muestra te dice las dos cosas.

`tests/support/fake_library.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include "domain/sample.h"

inline sampler::SampleLibrary makeLib(std::vector<uint64_t> lengths) {
    sampler::SampleLibrary lib;
    lib.sampleRate = 48000;
    for (size_t i = 0; i < lengths.size(); ++i) {
        sampler::Sample s;
        s.frameCount = lengths[i];
        s.name = "s" + std::to_string(i);
        s.frames.resize(lengths[i] * sampler::kChannels);
        for (uint64_t j = 0; j < lengths[i]; ++j) {
            float v = 0.1f * float(i + 1) + float(j) / 1000.0f;
            for (uint32_t c = 0; c < sampler::kChannels; ++c) {
                s.frames[j * sampler::kChannels + c] = v;
            }
        }
        lib.samples.push_back(std::move(s));
    }
    return lib;
}
```

- [ ] **Step 3: Escribir los tests que fallan**

`tests/test_transport_playback.cpp`:

```cpp
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
```

- [ ] **Step 4: Añadir los ficheros al Makefile**

Cambia estas dos líneas:

```make
DOMAIN_SRCS := src/domain/transport.cpp
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp
```

Y añade `-Itests` a `CXXFLAGS` para que `support/fake_library.h` resuelva:

```make
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Ithird_party -Isrc -Itests
```

- [ ] **Step 5: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO de compilación, `fatal error: 'domain/transport.h' file not found`.

- [ ] **Step 6: Escribir el Transport**

`src/domain/transport.h`:

```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include "domain/sample.h"

namespace sampler {

class Transport {
public:
    Transport(int sampleCount, uint32_t rampFrames);

    void step(int delta);
    void toggle();
    bool isPlaying() const;
    int armed() const;

    void render(float* out, uint32_t frames, const SampleLibrary& lib);

private:
    std::atomic<int> armedIndex_{0};
    std::atomic<bool> playRequested_{false};
    const int sampleCount_;
    const uint32_t rampFrames_;

    int cur_ = 0;
    uint64_t pos_ = 0;
    bool prevPlay_ = false;
};

}  // namespace sampler
```

`src/domain/transport.cpp`:

```cpp
#include "domain/transport.h"

#include <algorithm>

namespace sampler {

Transport::Transport(int sampleCount, uint32_t rampFrames)
    : sampleCount_(sampleCount), rampFrames_(rampFrames) {}

void Transport::step(int delta) {
    if (sampleCount_ <= 0) return;
    int next = armedIndex_.load(std::memory_order_relaxed) + delta;
    next = ((next % sampleCount_) + sampleCount_) % sampleCount_;
    armedIndex_.store(next, std::memory_order_release);
}

void Transport::toggle() {
    playRequested_.store(!playRequested_.load(std::memory_order_relaxed),
                         std::memory_order_release);
}

bool Transport::isPlaying() const {
    return playRequested_.load(std::memory_order_relaxed);
}

int Transport::armed() const {
    return armedIndex_.load(std::memory_order_relaxed);
}

void Transport::render(float* out, uint32_t frames, const SampleLibrary& lib) {
    const uint32_t outSamples = frames * kChannels;
    const bool play = playRequested_.load(std::memory_order_acquire);

    if (play && !prevPlay_) {
        cur_ = armedIndex_.load(std::memory_order_acquire);
        pos_ = 0;
    }
    prevPlay_ = play;

    const bool usable = play && sampleCount_ > 0 &&
                        static_cast<size_t>(sampleCount_) <= lib.samples.size();
    if (!usable) {
        pos_ = 0;
        std::fill(out, out + outSamples, 0.0f);
        return;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        const Sample& s = lib.samples[static_cast<size_t>(cur_)];
        if (pos_ >= s.frameCount) pos_ = 0;
        const float* src = &s.frames[pos_ * kChannels];
        for (uint32_t c = 0; c < kChannels; ++c) {
            out[i * kChannels + c] = src[c];
        }
        ++pos_;
    }
}

}  // namespace sampler
```

- [ ] **Step 7: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 6 test cases.

- [ ] **Step 8: Commit**

```bash
git add src/domain tests/support tests/test_transport_playback.cpp Makefile
git commit -m "feat(domain): sample types and transport playback with looping"
```

---

### Task 3: Cambio de sample cuantizado al final de la vuelta

La parte que de verdad define este programa: armar un índice mientras suena y que entre justo en el límite.

**Files:**
- Modify: `src/domain/transport.cpp`
- Create: `tests/test_transport_switching.cpp`
- Modify: `Makefile` (`TEST_SRCS`)

**Interfaces:**
- Consumes: todo lo de la Task 2, sin cambios en la firma pública.
- Produces: ningún símbolo nuevo. Cambia el comportamiento de `render` en el límite de vuelta.

- [ ] **Step 1: Escribir los tests que fallan**

`tests/test_transport_switching.cpp`:

```cpp
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
```

- [ ] **Step 2: Añadir el fichero al Makefile**

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp
```

- [ ] **Step 3: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO en "the armed change lands exactly at the loop boundary", con `out[2]` valiendo `0.100` en vez de `0.200`, porque `render` todavía reinicia el mismo sample en vez de leer el armado.

- [ ] **Step 4: Leer el índice armado en el límite de vuelta**

En `src/domain/transport.cpp`, sustituye la línea `if (pos_ >= s.frameCount) pos_ = 0;` y el acceso previo por esto:

```cpp
    for (uint32_t i = 0; i < frames; ++i) {
        if (pos_ >= lib.samples[static_cast<size_t>(cur_)].frameCount) {
            cur_ = armedIndex_.load(std::memory_order_acquire);
            pos_ = 0;
        }
        const Sample& s = lib.samples[static_cast<size_t>(cur_)];
        const float* src = &s.frames[pos_ * kChannels];
        for (uint32_t c = 0; c < kChannels; ++c) {
            out[i * kChannels + c] = src[c];
        }
        ++pos_;
    }
```

- [ ] **Step 5: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 14 test cases.

- [ ] **Step 6: Commit**

```bash
git add src/domain/transport.cpp tests/test_transport_switching.cpp Makefile
git commit -m "feat(domain): quantized sample switching at the loop boundary"
```

---

### Task 4: Rampas anti-click

Tres situaciones con trato distinto: rampa al arrancar, al parar y al cambiar de sample; nada cuando el loop se repite sobre sí mismo.

**Files:**
- Modify: `src/domain/transport.h`
- Modify: `src/domain/transport.cpp`
- Create: `tests/test_transport_ramps.cpp`
- Modify: `Makefile` (`TEST_SRCS`)

**Interfaces:**
- Consumes: el `Transport` de las Tasks 2 y 3. El parámetro `rampFrames` del constructor pasa a tener efecto.
- Produces: ningún símbolo público nuevo. Miembros privados `gain_`, `gainTarget_` y `gainStep_`.

**Convención de la rampa:** la ganancia se actualiza *antes* de aplicarse, así que con `rampFrames = 4` el primer frame sale a 0.25 y el cuarto a 1.0. Con `rampFrames = 0` la ganancia salta al objetivo de inmediato, que es lo que usan los tests de las tareas anteriores.

- [ ] **Step 1: Escribir los tests que fallan**

`tests/test_transport_ramps.cpp`:

```cpp
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
```

- [ ] **Step 2: Añadir el fichero al Makefile**

```make
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp
```

- [ ] **Step 3: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO en "starting playback fades in over rampFrames": `out[0]` vale `0.100` en lugar de `0.025`, porque la ganancia aún no existe.

- [ ] **Step 4: Añadir el estado de ganancia al header**

En `src/domain/transport.h`, dentro de la sección privada, debajo de `bool prevPlay_ = false;`:

```cpp
    float gain_ = 0.0f;
    float gainTarget_ = 0.0f;
    float gainStep_ = 0.0f;
```

- [ ] **Step 5: Implementar las rampas**

Sustituye el cuerpo completo de `Transport::render` en `src/domain/transport.cpp` por:

```cpp
void Transport::render(float* out, uint32_t frames, const SampleLibrary& lib) {
    const uint32_t outSamples = frames * kChannels;
    const bool play = playRequested_.load(std::memory_order_acquire);

    if (play && !prevPlay_) {
        cur_ = armedIndex_.load(std::memory_order_acquire);
        pos_ = 0;
        beginFade(1.0f);
    } else if (!play && prevPlay_) {
        beginFade(0.0f);
    }
    prevPlay_ = play;

    const bool haveLibrary = sampleCount_ > 0 &&
                             static_cast<size_t>(sampleCount_) <= lib.samples.size();
    const bool silent = !haveLibrary || (!play && gain_ <= 0.0f);
    if (silent) {
        pos_ = 0;
        if (!play) gain_ = 0.0f;
        std::fill(out, out + outSamples, 0.0f);
        return;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        const Sample& cs = lib.samples[static_cast<size_t>(cur_)];
        if (pos_ >= cs.frameCount) {
            const int next = armedIndex_.load(std::memory_order_acquire);
            if (next != cur_) {
                cur_ = next;
                beginFade(1.0f);
            }
            pos_ = 0;
        }

        const Sample& s = lib.samples[static_cast<size_t>(cur_)];
        if (play && gainTarget_ > 0.0f && rampFrames_ > 0) {
            const uint64_t fadeOutStart =
                s.frameCount > rampFrames_ ? s.frameCount - rampFrames_ : 0;
            if (pos_ >= fadeOutStart &&
                armedIndex_.load(std::memory_order_acquire) != cur_) {
                beginFade(0.0f);
            }
        }

        advanceGain();
        const float* src = &s.frames[pos_ * kChannels];
        for (uint32_t c = 0; c < kChannels; ++c) {
            out[i * kChannels + c] = src[c] * gain_;
        }
        ++pos_;
    }

    if (!play && gain_ <= 0.0f) pos_ = 0;
}
```

Y añade estos dos helpers privados justo encima, en el mismo `namespace`:

```cpp
void Transport::beginFade(float target) {
    gainTarget_ = target;
    if (rampFrames_ == 0) {
        gain_ = target;
        gainStep_ = 0.0f;
        return;
    }
    gainStep_ = (target - gain_) / static_cast<float>(rampFrames_);
}

void Transport::advanceGain() {
    if (gain_ == gainTarget_) return;
    gain_ += gainStep_;
    gain_ = std::min(std::max(gain_, 0.0f), 1.0f);
    if ((gainStep_ > 0.0f && gain_ >= gainTarget_) ||
        (gainStep_ < 0.0f && gain_ <= gainTarget_)) {
        gain_ = gainTarget_;
    }
}
```

Declara ambos en la sección privada de `src/domain/transport.h`, encima de los miembros de datos:

```cpp
    void beginFade(float target);
    void advanceGain();
```

Al arrancar, `beginFade(1.0f)` parte de `gain_ == 0`, así que el paso es `1/rampFrames` y el primer frame sale a `1/rampFrames`, que es la convención de arriba. Al cambiar de sample el fundido de salida ya ha dejado `gain_` en 0, así que el de entrada vuelve a partir de 0.

- [ ] **Step 6: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 20 test cases. Los tests de las Tasks 2 y 3 siguen verdes porque usan `rampFrames = 0`.

- [ ] **Step 7: Commit**

```bash
git add src/domain/transport.h src/domain/transport.cpp tests/test_transport_ramps.cpp Makefile
git commit -m "feat(domain): anti-click ramps on start, stop and sample switch"
```

---

### Task 5: Puertos y carga de WAV

Los cuatro headers de puerto y el primer adaptador: leer la carpeta y decodificar a float32 estéreo.

**Files:**
- Create: `src/ports/sample_repository.h`
- Create: `src/ports/audio_device.h`
- Create: `src/ports/input_source.h`
- Create: `src/ports/display.h`
- Create: `src/adapters/wav_folder_repository.h`
- Create: `src/adapters/wav_folder_repository.cpp`
- Create: `tests/test_wav_loader.cpp`
- Modify: `Makefile` (`ADAPTER_SRCS`, `TEST_SRCS`, enlazado de tests)

**Interfaces:**
- Consumes: `Sample`, `SampleLibrary`, `kChannels`, `Action` de la Task 2.
- Produces:
  - `struct LoadResult { SampleLibrary library; std::vector<std::string> failed; };`
  - `class SampleRepository { virtual LoadResult load(const std::string& folder, uint32_t sampleRate) = 0; };`
  - `class WavFolderRepository : public SampleRepository`.
  - `class AudioDevice` con `virtual bool start() = 0;`, `virtual void stop() = 0;`, `virtual uint32_t sampleRate() const = 0;`.
  - `class InputSource` con `virtual bool poll(int timeoutMs, Action& outAction) = 0;`.
  - `class Display` con `virtual void draw(int index, bool visible) = 0;`.

- [ ] **Step 1: Escribir los cuatro puertos**

`src/ports/sample_repository.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include "domain/sample.h"

namespace sampler {

struct LoadResult {
    SampleLibrary library;
    std::vector<std::string> failed;
};

class SampleRepository {
public:
    virtual ~SampleRepository() = default;
    virtual LoadResult load(const std::string& folder, uint32_t sampleRate) = 0;
};

}  // namespace sampler
```

`src/ports/audio_device.h`:

```cpp
#pragma once
#include <cstdint>

namespace sampler {

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual uint32_t sampleRate() const = 0;
};

}  // namespace sampler
```

`src/ports/input_source.h`:

```cpp
#pragma once
#include "domain/action.h"

namespace sampler {

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual bool poll(int timeoutMs, Action& outAction) = 0;
};

}  // namespace sampler
```

`src/ports/display.h`:

```cpp
#pragma once

namespace sampler {

class Display {
public:
    virtual ~Display() = default;
    virtual void draw(int index, bool visible) = 0;
};

}  // namespace sampler
```

- [ ] **Step 2: Escribir los tests que fallan**

El test genera sus propios WAV en un directorio temporal escribiendo la cabecera a mano, así no depende de ningún archivo del repo.

`tests/test_wav_loader.cpp`:

```cpp
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "adapters/wav_folder_repository.h"
#include "doctest.h"

namespace fs = std::filesystem;

static void writeWav(const fs::path& path, uint32_t rate, uint16_t channels,
                     uint32_t frameCount) {
    const uint16_t bits = 16;
    const uint32_t dataBytes = frameCount * channels * (bits / 8);
    const uint32_t byteRate = rate * channels * (bits / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * (bits / 8));
    const uint32_t riffSize = 36 + dataBytes;
    const uint32_t fmtSize = 16;
    const uint16_t pcm = 1;

    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); u32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(fmtSize); u16(pcm); u16(channels);
    u32(rate); u32(byteRate); u16(blockAlign); u16(bits);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < frameCount * channels; ++i) u16(0x1000);
}

static fs::path makeTempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("sampler_test_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TEST_CASE("loads wav files sorted alphabetically") {
    auto dir = makeTempDir("sorted");
    writeWav(dir / "02_b.wav", 48000, 2, 100);
    writeWav(dir / "01_a.wav", 48000, 2, 100);
    writeWav(dir / "notes.txt", 48000, 2, 100);

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    REQUIRE(r.library.samples.size() == 2);
    CHECK(r.library.samples[0].name == "01_a.wav");
    CHECK(r.library.samples[1].name == "02_b.wav");
    CHECK(r.failed.empty());
    fs::remove_all(dir);
}

TEST_CASE("converts mono to stereo at the requested sample rate") {
    auto dir = makeTempDir("mono");
    writeWav(dir / "a.wav", 44100, 1, 4410);

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    REQUIRE(r.library.samples.size() == 1);
    const auto& s = r.library.samples[0];
    CHECK(s.frames.size() == s.frameCount * sampler::kChannels);
    CHECK(s.frameCount > 4700);   // 0.1 s remuestreado a 48 kHz
    CHECK(s.frameCount < 5100);
    CHECK(r.library.sampleRate == 48000);
    fs::remove_all(dir);
}

TEST_CASE("a corrupt file is reported, not loaded") {
    auto dir = makeTempDir("corrupt");
    writeWav(dir / "good.wav", 48000, 2, 100);
    std::ofstream(dir / "bad.wav", std::ios::binary) << "this is not a wav";

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    CHECK(r.library.samples.size() == 1);
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("bad.wav") != std::string::npos);
    fs::remove_all(dir);
}

// Review Focus 4: un wav de 0 frames haria bucle infinito en el limite de vuelta
TEST_CASE("an empty wav is rejected instead of loaded") {
    auto dir = makeTempDir("empty");
    writeWav(dir / "silent.wav", 48000, 2, 0);
    writeWav(dir / "good.wav", 48000, 2, 100);

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    CHECK(r.library.samples.size() == 1);
    CHECK(r.library.samples[0].name == "good.wav");
    REQUIRE(r.failed.size() == 1);
    CHECK(r.failed[0].find("silent.wav") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("a folder with no wav files loads nothing and reports nothing") {
    auto dir = makeTempDir("nowavs");
    std::ofstream(dir / "readme.md") << "hello";

    sampler::WavFolderRepository repo;
    auto r = repo.load(dir.string(), 48000);

    CHECK(r.library.samples.empty());
    CHECK(r.failed.empty());
    fs::remove_all(dir);
}
```

- [ ] **Step 3: Enlazar miniaudio en el binario de tests**

En el Makefile, los tests pasan a necesitar miniaudio. Cambia:

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp

TEST_OBJS := $(addprefix $(BUILD)/,$(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(TEST_SRCS:.cpp=.o))
```

Y el enlazado de tests pasa a usar `$(LDLIBS)`:

```make
$(BUILD)/tests: $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)
```

- [ ] **Step 4: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO de compilación, `fatal error: 'adapters/wav_folder_repository.h' file not found`.

- [ ] **Step 5: Escribir el adaptador**

`src/adapters/wav_folder_repository.h`:

```cpp
#pragma once
#include "ports/sample_repository.h"

namespace sampler {

class WavFolderRepository : public SampleRepository {
public:
    LoadResult load(const std::string& folder, uint32_t sampleRate) override;
};

}  // namespace sampler
```

`src/adapters/wav_folder_repository.cpp`:

```cpp
#include "adapters/wav_folder_repository.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "miniaudio.h"

namespace sampler {
namespace {

constexpr ma_uint64 kReadChunkFrames = 4096;

bool decodeInto(const std::string& path, uint32_t sampleRate, Sample& out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kChannels, sampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) return false;

    std::vector<float> chunk(kReadChunkFrames * kChannels);
    for (;;) {
        ma_uint64 read = 0;
        const ma_result res =
            ma_decoder_read_pcm_frames(&decoder, chunk.data(), kReadChunkFrames, &read);
        if (read > 0) {
            out.frames.insert(out.frames.end(), chunk.begin(),
                              chunk.begin() + static_cast<size_t>(read * kChannels));
        }
        if (res != MA_SUCCESS || read == 0) break;
    }
    ma_decoder_uninit(&decoder);

    out.frameCount = out.frames.size() / kChannels;
    return out.frameCount > 0;
}

}  // namespace

LoadResult WavFolderRepository::load(const std::string& folder, uint32_t sampleRate) {
    namespace fs = std::filesystem;
    LoadResult result;
    result.library.sampleRate = sampleRate;

    std::vector<fs::path> paths;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        if (ext == ".wav") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& p : paths) {
        Sample s;
        s.name = p.filename().string();
        if (decodeInto(p.string(), sampleRate, s)) {
            result.library.samples.push_back(std::move(s));
        } else {
            result.failed.push_back(s.name);
        }
    }
    return result;
}

}  // namespace sampler
```

- [ ] **Step 6: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 25 test cases.

- [ ] **Step 7: Commit**

```bash
git add src/ports src/adapters/wav_folder_repository.h src/adapters/wav_folder_repository.cpp tests/test_wav_loader.cpp Makefile
git commit -m "feat(adapters): ports and wav folder loader via miniaudio"
```

---

### Task 6: Adaptador de dispositivo de audio

Abre el dispositivo, expone su frecuencia antes de cargar nada y conecta el callback con `Transport::render`.

**Files:**
- Create: `src/adapters/miniaudio_device.h`
- Create: `src/adapters/miniaudio_device.cpp`
- Modify: `Makefile` (`ADAPTER_SRCS`)

**Interfaces:**
- Consumes: `AudioDevice` (Task 5), `Transport` (Tasks 2-4), `SampleLibrary` (Task 2).
- Produces: `class MiniaudioDevice : public AudioDevice` con:
  - `bool open();` que inicializa el contexto y el dispositivo sin arrancarlo y deja `sampleRate()` disponible.
  - `void bind(Transport* transport, const SampleLibrary* library);`
  - `bool start();`, `void stop();`, `uint32_t sampleRate() const;`
  - `constexpr uint32_t kPeriodFrames = 256;`
  - `constexpr const char* kBackendEnvVar = "SAMPLER_AUDIO_BACKEND";`

**Backend forzable (spec §4).** Si la variable de entorno `SAMPLER_AUDIO_BACKEND` vale `alsa`, `pulseaudio`, `jack` o `coreaudio`, se usa ese backend; con cualquier otro valor, o sin la variable, miniaudio autodetecta. Importa en Linux, donde PulseAudio añade latencia frente a ALSA o JACK. No lleva test automático: el mapeo vive en el `.cpp` para no arrastrar `miniaudio.h` al header, y abrir un dispositivo real no es determinista. Se verifica a mano en la Task 10.

**Orden obligatorio de uso:** `open()` → leer `sampleRate()` → cargar la librería a esa frecuencia → `bind()` → `start()`. Hay que saber la frecuencia del dispositivo antes de decodificar, porque los samples se convierten a ella.

- [ ] **Step 1: Escribir el header**

`src/adapters/miniaudio_device.h`:

```cpp
#pragma once
#include "domain/sample.h"
#include "domain/transport.h"
#include "ports/audio_device.h"

struct ma_device;

namespace sampler {

constexpr uint32_t kPeriodFrames = 256;
constexpr const char* kBackendEnvVar = "SAMPLER_AUDIO_BACKEND";

class MiniaudioDevice : public AudioDevice {
public:
    MiniaudioDevice();
    ~MiniaudioDevice() override;

    bool open();
    void bind(Transport* transport, const SampleLibrary* library);

    bool start() override;
    void stop() override;
    uint32_t sampleRate() const override;

private:
    static void callback(ma_device* device, void* output, const void* input,
                         unsigned int frameCount);

    struct Impl;
    Impl* impl_;
    Transport* transport_ = nullptr;
    const SampleLibrary* library_ = nullptr;
    uint32_t sampleRate_ = 0;
    bool opened_ = false;
    bool started_ = false;
};

}  // namespace sampler
```

- [ ] **Step 2: Escribir la implementación**

`src/adapters/miniaudio_device.cpp`:

```cpp
#include "adapters/miniaudio_device.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>

#include "miniaudio.h"

namespace sampler {

struct MiniaudioDevice::Impl {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
};

namespace {

bool backendFromName(const char* name, ma_backend& out) {
    if (name == nullptr) return false;
    const std::string n(name);
    if (n == "alsa") { out = ma_backend_alsa; return true; }
    if (n == "pulseaudio") { out = ma_backend_pulseaudio; return true; }
    if (n == "jack") { out = ma_backend_jack; return true; }
    if (n == "coreaudio") { out = ma_backend_coreaudio; return true; }
    return false;
}

}  // namespace

MiniaudioDevice::MiniaudioDevice() : impl_(new Impl()) {}

MiniaudioDevice::~MiniaudioDevice() {
    stop();
    if (opened_) ma_device_uninit(&impl_->device);
    if (impl_->contextReady) ma_context_uninit(&impl_->context);
    delete impl_;
}

void MiniaudioDevice::callback(ma_device* device, void* output, const void*,
                               unsigned int frameCount) {
    auto* self = static_cast<MiniaudioDevice*>(device->pUserData);
    auto* out = static_cast<float*>(output);
    if (self->transport_ == nullptr || self->library_ == nullptr) {
        std::fill(out, out + frameCount * kChannels, 0.0f);
        return;
    }
    self->transport_->render(out, frameCount, *self->library_);
}

bool MiniaudioDevice::open() {
    ma_backend forced;
    const bool hasForced = backendFromName(std::getenv(kBackendEnvVar), forced);
    if (ma_context_init(hasForced ? &forced : nullptr, hasForced ? 1 : 0, nullptr,
                        &impl_->context) != MA_SUCCESS) {
        return false;
    }
    impl_->contextReady = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.periodSizeInFrames = kPeriodFrames;
    cfg.dataCallback = &MiniaudioDevice::callback;
    cfg.pUserData = this;

    if (ma_device_init(&impl_->context, &cfg, &impl_->device) != MA_SUCCESS) return false;
    opened_ = true;
    sampleRate_ = impl_->device.sampleRate;
    return true;
}

void MiniaudioDevice::bind(Transport* transport, const SampleLibrary* library) {
    transport_ = transport;
    library_ = library;
}

bool MiniaudioDevice::start() {
    if (!opened_ || started_) return started_;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) return false;
    started_ = true;
    return true;
}

void MiniaudioDevice::stop() {
    if (!started_) return;
    ma_device_stop(&impl_->device);
    started_ = false;
}

uint32_t MiniaudioDevice::sampleRate() const { return sampleRate_; }

}  // namespace sampler
```

- [ ] **Step 3: Añadir el fichero al Makefile**

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp
```

- [ ] **Step 4: Verificar que la variable de entorno llega al mapeo**

Run: `grep -n "SAMPLER_AUDIO_BACKEND\|backendFromName" src/adapters/miniaudio_device.h src/adapters/miniaudio_device.cpp`
Esperado: la constante en el header y dos referencias en el `.cpp`.

- [ ] **Step 5: Compilar y comprobar que los tests siguen verdes**

Run: `make test`
Esperado: PASS, 25 test cases. Este adaptador no tiene test propio: abrir un dispositivo real no es determinista y no se puede verificar en CI. Su comportamiento se valida a mano en la Task 10.

- [ ] **Step 6: Commit**

```bash
git add src/adapters/miniaudio_device.h src/adapters/miniaudio_device.cpp Makefile
git commit -m "feat(adapters): miniaudio playback device with forceable backend"
```

---

### Task 7: Configuración y mapa de teclas

Lee `sampler.conf` si existe y traduce nombres de tecla a las secuencias de bytes que manda la terminal.

**Files:**
- Create: `src/config.h`
- Create: `src/config.cpp`
- Create: `tests/test_config.cpp`
- Modify: `Makefile` (`APP_SRCS`, `TEST_SRCS`, `TEST_OBJS`)

**Interfaces:**
- Consumes: `Action` (Task 2).
- Produces:
  - `struct KeyBinding { std::string name; std::string bytes; std::string altBytes; };`
  - `struct Config { std::map<Action, KeyBinding> keys; uint32_t rampMs = kDefaultRampMs; uint32_t blinkMs = kDefaultBlinkMs; };`
  - `constexpr uint32_t kDefaultRampMs = 2;`
  - `constexpr uint32_t kDefaultBlinkMs = 250;`
  - `Config defaultConfig();`
  - `Config loadConfig(const std::string& path);` que parte de `defaultConfig()` y sobreescribe lo que encuentre.
  - `bool keyNameToBytes(const std::string& name, KeyBinding& out);`

Los nombres de tecla admitidos son `left`, `right`, `up`, `down`, `space`, `enter`, `esc` y cualquier carácter imprimible suelto. Las flechas se registran con sus dos secuencias posibles: `ESC [ C` y `ESC O C`.

- [ ] **Step 1: Escribir los tests que fallan**

`tests/test_config.cpp`:

```cpp
#include <filesystem>
#include <fstream>

#include "config.h"
#include "doctest.h"

using sampler::Action;

TEST_CASE("default config binds the three keys plus quit") {
    auto c = sampler::defaultConfig();
    CHECK(c.keys.at(Action::Prev).name == "left");
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Toggle).name == "space");
    CHECK(c.keys.at(Action::Quit).name == "q");
    CHECK(c.rampMs == sampler::kDefaultRampMs);
    CHECK(c.blinkMs == sampler::kDefaultBlinkMs);
}

TEST_CASE("arrow keys carry both escape sequences") {
    sampler::KeyBinding b;
    REQUIRE(sampler::keyNameToBytes("right", b));
    CHECK(b.bytes == "\x1b[C");
    CHECK(b.altBytes == "\x1bOC");
}

TEST_CASE("a single printable character binds to itself") {
    sampler::KeyBinding b;
    REQUIRE(sampler::keyNameToBytes("d", b));
    CHECK(b.bytes == "d");
    CHECK(b.altBytes.empty());
}

TEST_CASE("an unknown key name is rejected") {
    sampler::KeyBinding b;
    CHECK(sampler::keyNameToBytes("f13", b) == false);
}

TEST_CASE("a config file overrides the defaults it mentions") {
    auto path = std::filesystem::temp_directory_path() / "sampler_test.conf";
    {
        std::ofstream f(path);
        f << "# comentario\n"
          << "prev = a\n"
          << "next = d\n"
          << "blink_ms = 400\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.keys.at(Action::Prev).bytes == "a");
    CHECK(c.keys.at(Action::Next).bytes == "d");
    CHECK(c.keys.at(Action::Toggle).name == "space");
    CHECK(c.blinkMs == 400);
    CHECK(c.rampMs == sampler::kDefaultRampMs);
    std::filesystem::remove(path);
}

TEST_CASE("a missing config file yields the defaults") {
    auto c = sampler::loadConfig("/definitely/not/here.conf");
    CHECK(c.keys.at(Action::Toggle).name == "space");
}

TEST_CASE("an unparseable line is ignored, the rest still applies") {
    auto path = std::filesystem::temp_directory_path() / "sampler_bad.conf";
    {
        std::ofstream f(path);
        f << "garbage without equals\n"
          << "next = f13\n"
          << "quit = x\n";
    }
    auto c = sampler::loadConfig(path.string());
    CHECK(c.keys.at(Action::Next).name == "right");
    CHECK(c.keys.at(Action::Quit).bytes == "x");
    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

```make
APP_SRCS := src/config.cpp
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp

TEST_OBJS := $(addprefix $(BUILD)/,$(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o) $(TEST_SRCS:.cpp=.o))
```

Ojo: `APP_SRCS` acabará conteniendo `src/main.cpp` en la Task 10, y entonces `TEST_OBJS` tendría dos `main`. En la Task 10 se separa `src/main.cpp` de `APP_SRCS` para evitarlo.

- [ ] **Step 3: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO de compilación, `fatal error: 'config.h' file not found`.

- [ ] **Step 4: Escribir el header**

`src/config.h`:

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "domain/action.h"

namespace sampler {

constexpr uint32_t kDefaultRampMs = 2;
constexpr uint32_t kDefaultBlinkMs = 250;

struct KeyBinding {
    std::string name;
    std::string bytes;
    std::string altBytes;
};

struct Config {
    std::map<Action, KeyBinding> keys;
    uint32_t rampMs = kDefaultRampMs;
    uint32_t blinkMs = kDefaultBlinkMs;
};

bool keyNameToBytes(const std::string& name, KeyBinding& out);
Config defaultConfig();
Config loadConfig(const std::string& path);

}  // namespace sampler
```

- [ ] **Step 5: Escribir la implementación**

`src/config.cpp`:

```cpp
#include "config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sampler {
namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool actionFromName(const std::string& name, Action& out) {
    if (name == "prev") { out = Action::Prev; return true; }
    if (name == "next") { out = Action::Next; return true; }
    if (name == "toggle") { out = Action::Toggle; return true; }
    if (name == "quit") { out = Action::Quit; return true; }
    return false;
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

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        if (key == "ramp_ms") { c.rampMs = static_cast<uint32_t>(std::atoi(value.c_str())); continue; }
        if (key == "blink_ms") { c.blinkMs = static_cast<uint32_t>(std::atoi(value.c_str())); continue; }

        Action action;
        KeyBinding binding;
        if (actionFromName(key, action) && keyNameToBytes(value, binding)) {
            c.keys[action] = binding;
        }
    }
    return c;
}

}  // namespace sampler
```

- [ ] **Step 6: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 32 test cases.

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp Makefile
git commit -m "feat: key bindings and configuration file"
```

---

### Task 8: Teclado en modo raw

Pone la terminal en modo raw, espera con `poll()` y traduce los bytes leídos a una `Action`.

**Files:**
- Create: `src/adapters/termios_keyboard.h`
- Create: `src/adapters/termios_keyboard.cpp`
- Create: `tests/test_key_matching.cpp`
- Modify: `Makefile` (`ADAPTER_SRCS`, `TEST_SRCS`)

**Interfaces:**
- Consumes: `InputSource` (Task 5), `Config` y `KeyBinding` (Task 7), `Action` (Task 2).
- Produces:
  - `class TermiosKeyboard : public InputSource` con `explicit TermiosKeyboard(const Config& config);`, `bool enterRawMode();`, `void restore();`, `bool poll(int timeoutMs, Action& outAction) override;`
  - `bool matchAction(const Config& config, const char* buf, size_t len, Action& outAction);` expuesta en el header para poder testear el parseo sin tocar la terminal.

- [ ] **Step 1: Escribir los tests que fallan**

Solo se testea `matchAction`: el modo raw depende de una tty real y se valida a mano en la Task 10.

`tests/test_key_matching.cpp`:

```cpp
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
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp src/adapters/termios_keyboard.cpp
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp
```

- [ ] **Step 3: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO de compilación, `fatal error: 'adapters/termios_keyboard.h' file not found`.

- [ ] **Step 4: Escribir el header**

`src/adapters/termios_keyboard.h`:

```cpp
#pragma once
#include <termios.h>

#include <cstddef>

#include "config.h"
#include "ports/input_source.h"

namespace sampler {

bool matchAction(const Config& config, const char* buf, size_t len, Action& outAction);

class TermiosKeyboard : public InputSource {
public:
    explicit TermiosKeyboard(const Config& config);
    ~TermiosKeyboard() override;

    bool enterRawMode();
    void restore();

    bool poll(int timeoutMs, Action& outAction) override;

private:
    const Config& config_;
    termios original_{};
    bool raw_ = false;
};

}  // namespace sampler
```

- [ ] **Step 5: Escribir la implementación**

`src/adapters/termios_keyboard.cpp`:

```cpp
#include "adapters/termios_keyboard.h"

#include <poll.h>
#include <unistd.h>

#include <cstring>

namespace sampler {
namespace {
constexpr size_t kReadBufferBytes = 32;
}

bool matchAction(const Config& config, const char* buf, size_t len, Action& outAction) {
    for (const auto& [action, binding] : config.keys) {
        if (!binding.bytes.empty() && binding.bytes.size() == len &&
            std::memcmp(buf, binding.bytes.data(), len) == 0) {
            outAction = action;
            return true;
        }
        if (!binding.altBytes.empty() && binding.altBytes.size() == len &&
            std::memcmp(buf, binding.altBytes.data(), len) == 0) {
            outAction = action;
            return true;
        }
    }
    return false;
}

TermiosKeyboard::TermiosKeyboard(const Config& config) : config_(config) {}

TermiosKeyboard::~TermiosKeyboard() { restore(); }

bool TermiosKeyboard::enterRawMode() {
    if (raw_) return true;
    if (tcgetattr(STDIN_FILENO, &original_) != 0) return false;
    termios raw = original_;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    raw_ = true;
    return true;
}

void TermiosKeyboard::restore() {
    if (!raw_) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    raw_ = false;
}

bool TermiosKeyboard::poll(int timeoutMs, Action& outAction) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    const int ready = ::poll(&fd, 1, timeoutMs);
    if (ready <= 0) return false;

    char buf[kReadBufferBytes];
    const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return false;
    return matchAction(config_, buf, static_cast<size_t>(n), outAction);
}

}  // namespace sampler
```

Una pulsación llega como una lectura entera, así que comparar el buffer completo basta. Si llegasen dos teclas pegadas en la misma lectura, no coincidiría ninguna y se descarta: con tres teclas y pulsaciones humanas no pasa.

- [ ] **Step 6: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 37 test cases.

- [ ] **Step 7: Commit**

```bash
git add src/adapters/termios_keyboard.h src/adapters/termios_keyboard.cpp tests/test_key_matching.cpp Makefile
git commit -m "feat(adapters): raw mode keyboard input with poll"
```

---

### Task 9: Pantalla ANSI

Pantalla alternativa, cursor oculto y el número centrado.

**Files:**
- Create: `src/adapters/ansi_display.h`
- Create: `src/adapters/ansi_display.cpp`
- Create: `tests/test_ansi_display.cpp`
- Modify: `Makefile` (`ADAPTER_SRCS`, `TEST_SRCS`)

**Interfaces:**
- Consumes: `Display` (Task 5).
- Produces:
  - `struct Size { int cols; int rows; };`
  - `std::string frameFor(int index, bool visible, Size size);` función pura, es la que se testea.
  - `class AnsiDisplay : public Display` con `void enter();`, `void leave();`, `void draw(int index, bool visible) override;`
  - Constantes `kEnterScreen`, `kLeaveScreen`, `kHideCursor`, `kShowCursor`, `kClearScreen`.

- [ ] **Step 1: Escribir los tests que fallan**

`tests/test_ansi_display.cpp`:

```cpp
#include <string>

#include "adapters/ansi_display.h"
#include "doctest.h"

using sampler::frameFor;
using sampler::Size;

TEST_CASE("the frame centres the number") {
    auto s = frameFor(7, true, Size{21, 5});
    CHECK(s.find("\x1b[3;11H7") != std::string::npos);
}

TEST_CASE("a hidden number is drawn as blanks of the same width") {
    auto visible = frameFor(42, true, Size{21, 5});
    auto hidden = frameFor(42, false, Size{21, 5});
    CHECK(visible.find("42") != std::string::npos);
    CHECK(hidden.find("42") == std::string::npos);
    CHECK(hidden.find("\x1b[3;10H  ") != std::string::npos);
}

TEST_CASE("the frame clears the screen before drawing") {
    auto s = frameFor(0, true, Size{21, 5});
    CHECK(s.rfind(sampler::kClearScreen, 0) == 0);
}

// Review Focus 5: una terminal mas estrecha que el texto no puede dar columna negativa
TEST_CASE("a terminal narrower than the text clamps to column 1") {
    auto s = frameFor(12345, true, Size{3, 1});
    CHECK(s.find("\x1b[1;1H") != std::string::npos);
    CHECK(s.find(";0H") == std::string::npos);
    CHECK(s.find(";-") == std::string::npos);
}

TEST_CASE("a degenerate terminal size does not crash") {
    auto s = frameFor(1, true, Size{0, 0});
    CHECK(s.find("\x1b[1;1H") != std::string::npos);
}
```

- [ ] **Step 2: Añadir los ficheros al Makefile**

```make
ADAPTER_SRCS := src/adapters/miniaudio_impl.cpp src/adapters/wav_folder_repository.cpp src/adapters/miniaudio_device.cpp src/adapters/termios_keyboard.cpp src/adapters/ansi_display.cpp
TEST_SRCS := tests/test_main.cpp tests/test_transport_playback.cpp tests/test_transport_switching.cpp tests/test_transport_ramps.cpp tests/test_wav_loader.cpp tests/test_config.cpp tests/test_key_matching.cpp tests/test_ansi_display.cpp
```

- [ ] **Step 3: Ejecutar los tests y verificar que fallan**

Run: `make test`
Esperado: FALLO de compilación, `fatal error: 'adapters/ansi_display.h' file not found`.

- [ ] **Step 4: Escribir el header**

`src/adapters/ansi_display.h`:

```cpp
#pragma once
#include <string>

#include "ports/display.h"

namespace sampler {

constexpr const char* kEnterScreen = "\x1b[?1049h";
constexpr const char* kLeaveScreen = "\x1b[?1049l";
constexpr const char* kHideCursor = "\x1b[?25l";
constexpr const char* kShowCursor = "\x1b[?25h";
constexpr const char* kClearScreen = "\x1b[2J";

struct Size {
    int cols;
    int rows;
};

Size terminalSize();
std::string frameFor(int index, bool visible, Size size);

class AnsiDisplay : public Display {
public:
    ~AnsiDisplay() override;

    void enter();
    void leave();
    void draw(int index, bool visible) override;

private:
    bool entered_ = false;
};

}  // namespace sampler
```

- [ ] **Step 5: Escribir la implementación**

`src/adapters/ansi_display.cpp`:

```cpp
#include "adapters/ansi_display.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

namespace sampler {
namespace {
constexpr int kFallbackCols = 80;
constexpr int kFallbackRows = 24;

void writeOut(const std::string& s) {
    ::fwrite(s.data(), 1, s.size(), stdout);
    ::fflush(stdout);
}
}  // namespace

Size terminalSize() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return Size{ws.ws_col, ws.ws_row};
    }
    return Size{kFallbackCols, kFallbackRows};
}

std::string frameFor(int index, bool visible, Size size) {
    const std::string text = std::to_string(index);
    const std::string body = visible ? text : std::string(text.size(), ' ');

    const int col = std::max(1, (size.cols - static_cast<int>(text.size())) / 2 + 1);
    const int row = std::max(1, size.rows / 2 + 1);

    return std::string(kClearScreen) + "\x1b[" + std::to_string(row) + ";" +
           std::to_string(col) + "H" + body;
}

AnsiDisplay::~AnsiDisplay() { leave(); }

void AnsiDisplay::enter() {
    if (entered_) return;
    writeOut(std::string(kEnterScreen) + kHideCursor + kClearScreen);
    entered_ = true;
}

void AnsiDisplay::leave() {
    if (!entered_) return;
    writeOut(std::string(kShowCursor) + kLeaveScreen);
    entered_ = false;
}

void AnsiDisplay::draw(int index, bool visible) {
    writeOut(frameFor(index, visible, terminalSize()));
}

}  // namespace sampler
```

Comprueba la aritmética contra el primer test: con `cols = 21` y el texto `"7"` de un carácter, `col = (21 - 1) / 2 + 1 = 11`; con `rows = 5`, `row = 5 / 2 + 1 = 3`. Da `\x1b[3;11H7`.

- [ ] **Step 6: Ejecutar los tests y verificar que pasan**

Run: `make test`
Esperado: PASS, 42 test cases.

- [ ] **Step 7: Commit**

```bash
git add src/adapters/ansi_display.h src/adapters/ansi_display.cpp tests/test_ansi_display.cpp Makefile
git commit -m "feat(adapters): ansi terminal display with centred index"
```

---

### Task 10: Composición, señales y el bucle principal

Junta todas las piezas en un ejecutable y añade lo que no se puede testear en aislamiento: el orden de arranque, los errores de carga y la restauración de la terminal.

**Files:**
- Create: `src/main.cpp`
- Create: `README.md`
- Create: `sampler.conf.example`
- Modify: `Makefile` (separar `src/main.cpp` de `APP_SRCS`)

**Interfaces:**
- Consumes: todo lo anterior. Respeta el orden obligatorio de la Task 6: `open()` → `sampleRate()` → `load()` → `bind()` → `start()`.
- Produces: el ejecutable `build/sampler`, que se invoca como `./build/sampler <carpeta> [sampler.conf]`.

- [ ] **Step 1: Arreglar el Makefile para que los tests no arrastren main**

```make
APP_SRCS := src/config.cpp
MAIN_SRC := src/main.cpp

APP_OBJS := $(addprefix $(BUILD)/,$(DOMAIN_SRCS:.cpp=.o) $(ADAPTER_SRCS:.cpp=.o) $(APP_SRCS:.cpp=.o) $(MAIN_SRC:.cpp=.o))
```

`TEST_OBJS` se queda como está, sin `MAIN_SRC`.

- [ ] **Step 2: Escribir el main**

`src/main.cpp`:

```cpp
#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <string>

#include "adapters/ansi_display.h"
#include "adapters/miniaudio_device.h"
#include "adapters/termios_keyboard.h"
#include "adapters/wav_folder_repository.h"
#include "config.h"
#include "domain/transport.h"

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
    const sampler::Config config = sampler::loadConfig(configPath);

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
        if (keyboard.poll(kPollTimeoutMs, action)) {
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

- [ ] **Step 3: Compilar el ejecutable**

Run: `make`
Esperado: compila y produce `build/sampler` sin errores ni warnings de nuestro código.

- [ ] **Step 4: Comprobar que los tests siguen verdes**

Run: `make test`
Esperado: PASS, 42 test cases.

- [ ] **Step 5: Verificación manual con audio real**

Prepara tres WAV de duraciones distintas en una carpeta y ejecuta `./build/sampler ./mis_samples`. Comprueba una por una:

1. Arranca mostrando `0` fijo, sin sonido.
2. Flecha derecha: el número pasa a `1`, sigue sin sonar.
3. Espacio: empieza a sonar el `1` desde el principio y el número parpadea.
4. Flecha derecha dos veces seguidas mientras suena: el número salta a `3` al instante, pero el audio sigue con el `1` hasta que termina su vuelta, y entonces entra el `3`.
5. El cambio no produce chasquido ni silencio audible.
6. Espacio otra vez: para sin chasquido y el número deja de parpadear.
7. Espacio de nuevo: arranca desde el principio del `3`.
8. Flecha izquierda desde el `0`: salta al último.
9. `Ctrl-C`: la terminal vuelve a la normalidad, con eco y cursor visibles. Comprueba con `echo hola`.
10. `q`: sale igual de limpio.
11. En Linux, `SAMPLER_AUDIO_BACKEND=alsa ./build/sampler ./mis_samples` arranca y suena. Prueba también con `pulseaudio` y con un valor inválido como `patata`, que debe caer en la autodetección en lugar de fallar.

- [ ] **Step 6: Escribir el README y la config de ejemplo**

`sampler.conf.example`:

```
# copia este fichero a sampler.conf y ajusta lo que quieras
prev   = left
next   = right
toggle = space
quit   = q

# duracion de las rampas anti-click, en milisegundos
ramp_ms = 2

# medio periodo del parpadeo, en milisegundos
blink_ms = 250
```

`README.md`:

```markdown
# sampler

Reproductor de loops por teclado para tocar en directo. Carga una carpeta de
WAV en RAM y permite encadenarlos con tres teclas, cambiando siempre en el
límite de la vuelta para que el audio no se corte.

## Compilar

    make

Funciona en macOS y Linux. No hace falta instalar nada: miniaudio y doctest
viven en `third_party/`.

## Usar

    ./build/sampler ./mis_samples [sampler.conf]

- **Flechas**: mueven el índice. Si está parado, en silencio. Si está sonando,
  arman el destino y el cambio entra al acabar la vuelta. Las pulsaciones se
  acumulan.
- **Espacio**: arranca desde el principio, o para y resetea.
- **q** o **Ctrl-C**: salir.

El número de la pantalla parpadea mientras suena y se queda fijo al parar.

Los samples se ordenan alfabéticamente por nombre, así que conviene nombrarlos
con ceros delante (`01_`, `02_`, `10_`) para que el índice sea estable.

## Tests

    make test
```

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp README.md sampler.conf.example Makefile
git commit -m "feat: wire everything into the sampler executable"
```
