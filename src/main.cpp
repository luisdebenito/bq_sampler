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
                case sampler::Action::Jump:   transport.jump(); break;
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
