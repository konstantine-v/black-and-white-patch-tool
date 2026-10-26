// BWLaunch.exe: starts runblack.exe suspended, loads BWDecomp.dll into it, then lets the game run.
//
//     BWLaunch.exe [--game <path\to\runblack.exe>] [-- <arguments for the game>]
//
// Without --game it starts the runblack.exe next to itself. The game's files are never modified.

#include "core/CommandLine.h"
#include "core/GameExecutable.h"
#include "core/Handle.h"
#include "core/Module.h"
#include "core/Unicode.h"
#include "core/Windows.h"

#include <shellapi.h>

#include <filesystem>
#include <string>

static_assert(sizeof(void*) == 4, "the launcher must be 32-bit so kernel32!LoadLibraryW matches the game's");

namespace {

constexpr wchar_t kTitle[] = L"Black & White patch";

void ShowError(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

std::wstring LastErrorText()
{
    return L" (Windows error " + std::to_wstring(GetLastError()) + L")";
}

struct Options {
    std::filesystem::path game;
    std::wstring gameArguments;
};

Options ParseCommandLine(const std::filesystem::path& launcherDirectory)
{
    Options options{launcherDirectory / L"runblack.exe", {}};
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) {
        return options;
    }
    for (int i = 1; i < count; ++i) {
        const std::wstring_view argument = arguments[i];
        if (argument == L"--game" && i + 1 < count) {
            options.game = arguments[++i];
        }
        else if (argument == L"--") {
            for (++i; i < count; ++i) {
                options.gameArguments += L' ';
                options.gameArguments += bw::QuoteArgument(arguments[i]);
            }
        }
    }
    LocalFree(arguments);
    return options;
}

/// Loads `dll` in `process` with a remote LoadLibraryW call. The main thread is still suspended, so the DLL runs
/// before any game code.
bool InjectLibrary(HANDLE process, const std::filesystem::path& dll, std::wstring& error)
{
    const std::wstring path = dll.wstring();
    const SIZE_T size = (path.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remotePath == nullptr) {
        error = L"Cannot allocate memory in the game process" + LastErrorText();
        return false;
    }
    struct RemoteFree {
        HANDLE process;
        void* memory;
        ~RemoteFree() { VirtualFreeEx(process, memory, 0, MEM_RELEASE); }
    } const freeRemotePath{process, remotePath};
    if (!WriteProcessMemory(process, remotePath, path.c_str(), size, nullptr)) {
        error = L"Cannot write into the game process" + LastErrorText();
        return false;
    }

    // kernel32 is mapped at the same address in every process of a session, so the launcher's LoadLibraryW is the
    // game's (both are 32-bit, see the static_assert above).
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (loadLibrary == nullptr) {
        error = L"Cannot find LoadLibraryW" + LastErrorText();
        return false;
    }
    const bw::UniqueHandle thread(CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr));
    if (!thread) {
        error = L"Cannot start the loader thread" + LastErrorText();
        return false;
    }
    DWORD module = 0;
    if (WaitForSingleObject(thread.Get(), INFINITE) != WAIT_OBJECT_0 || !GetExitCodeThread(thread.Get(), &module)) {
        error = L"The loader thread failed" + LastErrorText();
        return false;
    }

    if (module == 0) {
        error = L"BWDecomp.dll refused to load into the game. See BWDecomp.log next to it for the reason.";
        return false;
    }
    return true;
}

int Run()
{
    const std::filesystem::path launcherDirectory = bw::ModuleDirectory(nullptr);
    const Options options = ParseCommandLine(launcherDirectory);
    const std::filesystem::path dll = launcherDirectory / L"BWDecomp.dll";

    if (!std::filesystem::exists(options.game)) {
        ShowError(L"Cannot find " + options.game.wstring() +
                  L".\n\nPlace BWLaunch.exe and BWDecomp.dll in the Black & White folder, or pass --game.");
        return 1;
    }
    if (!std::filesystem::exists(dll)) {
        ShowError(L"Cannot find " + dll.wstring() + L".");
        return 1;
    }
    if (const std::wstring problem = bw::DescribeGameExecutable(bw::CheckGameExecutable(options.game), options.game);
        !problem.empty()) {
        ShowError(problem);
        return 1;
    }

    std::wstring commandLine = bw::QuoteArgument(options.game.wstring()) + options.gameArguments;
    const std::wstring workingDirectory = options.game.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION started{};
    if (!CreateProcessW(options.game.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        workingDirectory.c_str(), &startup, &started)) {
        ShowError(L"Cannot start " + options.game.wstring() + LastErrorText());
        return 1;
    }
    const bw::UniqueHandle process(started.hProcess);
    const bw::UniqueHandle mainThread(started.hThread);

    std::wstring error;
    if (!InjectLibrary(process.Get(), dll, error)) {
        TerminateProcess(process.Get(), 1);
        ShowError(error);
        return 1;
    }
    if (ResumeThread(mainThread.Get()) == static_cast<DWORD>(-1)) {
        const std::wstring reason = LastErrorText();
        TerminateProcess(process.Get(), 1);
        ShowError(L"Cannot start the game's main thread" + reason);
        return 1;
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try {
        return Run();
    }
    catch (const std::exception& exception) {
        ShowError(L"Unexpected error: " + bw::Widen(exception.what()));
        return 1;
    }
}
