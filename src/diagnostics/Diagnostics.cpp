#include "diagnostics/Diagnostics.h"

#include "core/Image.h"
#include "core/Log.h"
#include "core/Patch.h"

#include <intrin.h>

#include <atomic>
#include <string>
#include <string_view>

namespace bw::diagnostics {

namespace {

image::Section g_text{};

decltype(&::ExitProcess) g_exitProcess = nullptr;
decltype(&::MessageBoxA) g_messageBoxA = nullptr;
decltype(&::PostQuitMessage) g_postQuitMessage = nullptr;
decltype(&::OutputDebugStringA) g_outputDebugStringA = nullptr;

std::atomic<int> g_exceptionsLogged = 0;
constexpr int kMaxExceptionsLogged = 16;

/// Reads a stack slot, tolerating the end of the stack. Separate function: __try cannot share a frame with objects
/// that need unwinding.
bool ReadSlot(const std::uint32_t* slot, std::uint32_t& value) noexcept
{
    __try {
        value = *slot;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/// Logs the return addresses into original code found in the 256 stack slots above `stack`: a crude but
/// dependable call stack for code without frame pointers. Map them with symbols/functions.csv.
void LogStackScan(const std::uint32_t* stack) noexcept
{
    std::string line;
    int found = 0;
    for (int slot = 0; slot < 256 && found < 24; ++slot) {
        std::uint32_t value = 0;
        if (!ReadSlot(stack + slot, value)) {
            break;
        }
        if (value >= g_text.begin && value < g_text.end) {
            line += std::format(" {:08x}", value);
            ++found;
        }
    }
    log::Error("  stack (original code):{}", line);
}

LONG CALLBACK LogException(EXCEPTION_POINTERS* exception) noexcept
{
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    // Debugger notifications, thread naming and C++ exceptions (handled inside MFC/our CRT) are not failures.
    if (code == DBG_PRINTEXCEPTION_C || code == 0x4001000A || code == 0x406D1388 || code == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_exceptionsLogged.fetch_add(1) >= kMaxExceptionsLogged) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const CONTEXT& context = *exception->ContextRecord;
    log::Error("Exception 0x{:08x} at 0x{:08x} (first chance)", code,
               reinterpret_cast<Address>(exception->ExceptionRecord->ExceptionAddress));
    if (code == EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters >= 2) {
        log::Error("  {} 0x{:08x}", exception->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
                   exception->ExceptionRecord->ExceptionInformation[1]);
    }
    log::Error("  eax={:08x} ebx={:08x} ecx={:08x} edx={:08x} esi={:08x} edi={:08x} ebp={:08x} esp={:08x}",
               context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi, context.Edi, context.Ebp, context.Esp);
    LogStackScan(reinterpret_cast<const std::uint32_t*>(context.Esp));
    return EXCEPTION_CONTINUE_SEARCH;
}

void WINAPI ExitProcessTraced(UINT exitCode)
{
    log::Info("ExitProcess({}) called from 0x{:08x}", exitCode, reinterpret_cast<Address>(_ReturnAddress()));
    LogStackScan(static_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    g_exitProcess(exitCode);
}

int WINAPI MessageBoxATraced(HWND window, LPCSTR text, LPCSTR caption, UINT type)
{
    log::Info("MessageBoxA from 0x{:08x}: [{}] {}", reinterpret_cast<Address>(_ReturnAddress()),
              caption != nullptr ? caption : "", text != nullptr ? text : "");
    const int result = g_messageBoxA(window, text, caption, type);
    log::Info("MessageBoxA returned {}", result);
    return result;
}

void WINAPI PostQuitMessageTraced(int exitCode)
{
    log::Info("PostQuitMessage({}) called from 0x{:08x}", exitCode, reinterpret_cast<Address>(_ReturnAddress()));
    LogStackScan(static_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    g_postQuitMessage(exitCode);
}

void WINAPI OutputDebugStringATraced(LPCSTR text)
{
    if (text != nullptr) {
        std::string_view message(text);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.remove_suffix(1);
        }
        log::Info("[game] {}", message);
    }
    g_outputDebugStringA(text);
}

template <typename Function>
void Replace(HMODULE game, std::string_view dll, std::string_view name, Function replacement, Function& original)
{
    original = patch::ReplaceImport(game, dll, name, replacement);
    if (original == nullptr) {
        log::Warning("Diagnostics: {}!{} is not imported", dll, name);
    }
}

}  // namespace

void Install(HMODULE game, const Config& config) noexcept
{
    if (const std::optional<image::Section> text = image::FindSection(game, ".text")) {
        g_text = *text;
    }
    if (config.traceExit) {
        Replace(game, "KERNEL32.dll", "ExitProcess", &ExitProcessTraced, g_exitProcess);
        Replace(game, "USER32.dll", "MessageBoxA", &MessageBoxATraced, g_messageBoxA);
        Replace(game, "USER32.dll", "PostQuitMessage", &PostQuitMessageTraced, g_postQuitMessage);
    }
    if (config.traceExceptions) {
        AddVectoredExceptionHandler(0, &LogException);
    }
    if (config.forwardDebugOutput) {
        Replace(game, "KERNEL32.dll", "OutputDebugStringA", &OutputDebugStringATraced, g_outputDebugStringA);
    }
}

}  // namespace bw::diagnostics
