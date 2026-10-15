#pragma once
#include <atomic>
#include <string>

#include "config.h"

namespace sampler {

enum class WizardOutcome { Saved, Unsaved, Cancelled, NoTerminal };

WizardOutcome runSetupWizard(const std::string& path, const std::atomic<bool>& cancel,
                             Config& out);

}  // namespace sampler
