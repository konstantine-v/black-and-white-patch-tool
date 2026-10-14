// BWLaunch.exe: starts runblack.exe suspended, loads BWDecomp.dll into it, then lets the game run.
//
//     BWLaunch.exe [--game <path\to\runblack.exe>] [-- <arguments for the game>]
//
// Without --game it starts the runblack.exe next to itself. The game's files are never modified.

#include "core/Image.h"
#include "core/Windows.h"

#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

static_assert(sizeof(void*) == 4, "the launcher must be 32-bit so kernel32!LoadLibraryW matches the game's");

namespace {

constexpr wchar_t kTitle[] = L"Black & White decompilation";

void ShowError(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

std::wstring Widen(std::string_view text)
{
    return {text.begin(), text.end()};
}

std::wstring LastErrorText()
{
    return L" (Windows error " + std::to_wstring(GetLastError()) + L")";
}

std::filesystem::path ExecutableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())));
    return std::filesystem::path(path).parent_path();
}

std::optional<std::string> FileSha256(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto digest = bw::image::ComputeSha256(std::as_bytes(std::span(bytes)));
    if (!digest) {
        return std::nullopt;
    }
    const auto hex = bw::image::ToHex(*digest);
    return std::string(hex.data(), hex.size());
}

/// Explains why `game` cannot be used, or returns nullopt when it is the supported build.
std::optional<std::wstring> CheckGameExecutable(const std::filesystem::path& game)
{
    const std::optional<std::string> hash = FileSha256(game);
    if (!hash) {
        return L"Cannot read " + game.wstring() + L".";
    }
    if (*hash == bw::image::kSupportedFileSha256) {
        return std::nullopt;
    }
    if (*hash == bw::image::kRetailSafeDiscFileSha256) {
        return L"This runblack.exe is the SafeDisc-protected retail build. SafeDisc needs the secdrv.sys driver, "
               L"which current Windows versions refuse to load, so this file cannot start.\n\n"
               L"The decompilation targets the unprotected build of the same version that is on the Black & White "
               L"disc (crack\\runblack.exe). Copy that file over the installed runblack.exe and start again.";
    }
    return L"Unsupported runblack.exe (SHA-256 " + Widen(*hash) +
           L").\n\nThe decompilation only supports Black & White 1.00 (English) with SHA-256 " +
           Widen(bw::image::kSupportedFileSha256) + L".";
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
    for (int i = 1; i < count; ++i) {
        const std::wstring_view argument = arguments[i];
        if (argument == L"--game" && i + 1 < count) {
            options.game = arguments[++i];
        }
        else if (argument == L"--") {
            for (++i; i < count; ++i) {
                options.gameArguments += L" \"";
                options.gameArguments += arguments[i];
                options.gameArguments += L'"';
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
    if (remotePath == nullptr || !WriteProcessMemory(process, remotePath, path.c_str(), size, nullptr)) {
        error = L"Cannot write into the game process" + LastErrorText();
        return false;
    }

    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (thread == nullptr) {
        error = L"Cannot start the loader thread" + LastErrorText();
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }
    WaitForSingleObject(thread, INFINITE);
    DWORD module = 0;
    GetExitCodeThread(thread, &module);
    CloseHandle(thread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);

    if (module == 0) {
        error = L"BWDecomp.dll refused to load into the game. See BWDecomp.log next to it for the reason.";
        return false;
    }
    return true;
}

int Run()
{
    const std::filesystem::path launcherDirectory = ExecutableDirectory();
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
    if (const std::optional<std::wstring> problem = CheckGameExecutable(options.game)) {
        ShowError(*problem);
        return 1;
    }

    std::wstring commandLine = L"\"" + options.game.wstring() + L"\"" + options.gameArguments;
    const std::wstring workingDirectory = options.game.parent_path().wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(options.game.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        workingDirectory.c_str(), &startup, &process)) {
        ShowError(L"Cannot start " + options.game.wstring() + LastErrorText());
        return 1;
    }

    std::wstring error;
    if (!InjectLibrary(process.hProcess, dll, error)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        ShowError(error);
        return 1;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try {
        return Run();
    }
    catch (const std::exception& exception) {
        ShowError(L"Unexpected error: " + Widen(exception.what()));
        return 1;
    }
}
