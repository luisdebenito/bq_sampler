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
        case Action::Jump:   return "SALTAR YA";
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
