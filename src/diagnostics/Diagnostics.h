#pragma once

#include "core/Config.h"
#include "core/Windows.h"

/// Run-time diagnostics for investigating the original code without a debugger: who ends the process or shows a
/// message box, crashes with a stack scan mapped to original addresses, and the game's own debug output.
namespace bw::diagnostics {

void Install(HMODULE game, const Config& config) noexcept;

}  // namespace bw::diagnostics
