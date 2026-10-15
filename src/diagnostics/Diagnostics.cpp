#include "diagnostics/Diagnostics.h"

#include "core/Handle.h"
#include "core/Image.h"
#include "core/Log.h"
#include "core/Patch.h"
#include "diagnostics/Screenshot.h"

#include <tlhelp32.h>

#include <intrin.h>

#include <array>
#include <atomic>
#include <span>
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

constexpr std::size_t kStackSlots = 256;
using StackCopy = std::array<std::uint32_t, kStackSlots>;

/// Copies up to kStackSlots slots from `stack`, stopping at the end of the stack; returns the number copied.
std::size_t CopyStack(const std::uint32_t* stack, StackCopy& copy) noexcept
{
    std::size_t slot = 0;
    while (slot < copy.size() && ReadSlot(stack + slot, copy[slot])) {
        ++slot;
    }
    return slot;
}

/// Logs the values in `slots` that point into original code: a crude but dependable call stack for code without
/// frame pointers. Map them with symbols/functions.csv.
void LogCodeAddresses(std::span<const std::uint32_t> slots, log::Level level = log::Level::Error) noexcept
{
    // A stack buffer: this runs inside exception handlers, where the heap may be locked by the faulting thread.
    constexpr int kMaxAddresses = 24;
    std::array<char, kMaxAddresses * 9> line{};
    char* cursor = line.data();
    int found = 0;
    for (const std::uint32_t value : slots) {
        if (value >= g_text.begin && value < g_text.end) {
            cursor = std::format_to_n(cursor, 9, " {:08x}", value).out;
            if (++found == kMaxAddresses) {
                break;
            }
        }
    }
    log::Format(level, "  stack (original code):{}",
                std::string_view(line.data(), static_cast<std::size_t>(cursor - line.data())));
}

void LogStackScan(const std::uint32_t* stack, log::Level level = log::Level::Error) noexcept
{
    StackCopy copy{};
    LogCodeAddresses(std::span(copy).first(CopyStack(stack, copy)), level);
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
    LogStackScan(static_cast<const std::uint32_t*>(_AddressOfReturnAddress()), log::Level::Info);
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
    LogStackScan(static_cast<const std::uint32_t*>(_AddressOfReturnAddress()), log::Level::Info);
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
    InstallScreenshots(config);
}

void LogThreadStacks() noexcept
{
    struct ThreadState {
        DWORD id = 0;
        bool captured = false;
        std::uint32_t eip = 0;
        std::size_t slots = 0;
        StackCopy stack{};
    };
    try {
        std::vector<ThreadState> threads;
        {
            const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
            if (!snapshot) {
                log::Error("Thread list unavailable ({})", GetLastError());
                return;
            }
            THREADENTRY32 entry{};
            entry.dwSize = sizeof entry;
            for (BOOL more = Thread32First(snapshot.Get(), &entry); more; more = Thread32Next(snapshot.Get(), &entry)) {
                if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
                    threads.push_back({entry.th32ThreadID});
                }
            }
        }

        // While a thread is suspended nothing here allocates or logs: it may hold the heap or the log lock.
        for (ThreadState& state : threads) {
            const UniqueHandle thread(OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, state.id));
            if (!thread || SuspendThread(thread.Get()) == static_cast<DWORD>(-1)) {
                continue;
            }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(thread.Get(), &context)) {
                state.captured = true;
                state.eip = context.Eip;
                state.slots = CopyStack(reinterpret_cast<const std::uint32_t*>(context.Esp), state.stack);
            }
            ResumeThread(thread.Get());
        }

        for (const ThreadState& state : threads) {
            if (!state.captured) {
                log::Error("Thread {}: not captured", state.id);
                continue;
            }
            log::Error("Thread {}: eip 0x{:08x}", state.id, state.eip);
            LogCodeAddresses(std::span(state.stack).first(state.slots));
        }
    }
    catch (...) {
        log::Error("Thread list failed");
    }
}

}  // namespace bw::diagnostics
