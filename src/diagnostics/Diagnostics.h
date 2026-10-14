#pragma once

#include "core/Config.h"
#include "core/Windows.h"

/// Run-time diagnostics for investigating the original code without a debugger: who ends the process or shows a
/// message box, crashes with a stack scan mapped to original addresses, and the game's own debug output.
namespace bw::diagnostics {

void Install(HMODULE game, const Config& config) noexcept;

/// Logs every other thread's instruction pointer and the return addresses into original code on its stack. Each
/// thread is suspended only while its registers and stack are copied; nothing is allocated meanwhile, so a thread
/// holding the heap lock cannot deadlock the caller.
void LogThreadStacks() noexcept;

}  // namespace bw::diagnostics
