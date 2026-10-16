// BWDecompSetup.exe: installs the decompilation into a Black & White folder; installed as BWDecomp\Uninstall.exe, it
// removes it again.
//
//     BWDecompSetup.exe [/quiet] [/dir <game folder>] [/nostartmenu] [/desktop]
//     Uninstall.exe /uninstall [/quiet] [/dir <game folder>]
//
// /quiet shows no window and starts nothing: the exit code says what happened (0 done, 1 failed, 2 cancelled) and
// messages go to the console setup was started from. Internal: /elevated <install|uninstall> <folder> <result file>
// does the file work with administrator rights when the game folder needs them; /wait <process id> is how the
// uninstaller that copied itself out of the game folder waits for the original to exit.

#include "core/CommandLine.h"
#include "core/GameExecutable.h"
#include "core/Handle.h"
#include "core/Module.h"
#include "core/Unicode.h"
#include "core/Version.h"
#include "core/Windows.h"
#include "setup/Install.h"
#include "setup/Payload.h"
#include "setup/Resource.h"
#include "setup/System.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace bw::setup;
using Microsoft::WRL::ComPtr;

enum ExitCode : int { kDone = 0, kFailed = 1, kCancelled = 2 };

constexpr wchar_t kTitle[] = L"Black & White Decompilation Setup";
constexpr wchar_t kUninstallTitle[] = L"Black & White Decompilation";
constexpr wchar_t kTemporaryUninstallerPrefix[] = L"BWDecomp-Uninstall-";

struct Arguments {
    bool quiet = false;
    bool uninstall = false;
    bool startMenu = true;
    bool desktop = false;
    std::filesystem::path folder;
    std::wstring elevatedAction;  // "install" or "uninstall"
    std::filesystem::path resultFile;
    DWORD waitFor = 0;
};

/// An absolute folder with long names (a /dir argument may be relative or use 8.3 names such as PROGRA~2), without a
/// trailing separator.
std::filesystem::path FullFolder(const std::filesystem::path& folder)
{
    std::error_code error;
    std::filesystem::path full = std::filesystem::absolute(folder, error);
    if (error) {
        return folder;
    }
    std::wstring longPath(32768, wchar_t{});
    const DWORD length = GetLongPathNameW(full.c_str(), longPath.data(), static_cast<DWORD>(longPath.size()));
    if (length > 0 && length < longPath.size()) {
        longPath.resize(length);
        full = std::move(longPath);
    }
    full = full.lexically_normal();
    if (!full.has_filename() && full.has_relative_path()) {
        full = full.parent_path();
    }
    return full;
}

/// A folder typed into the dialog or given with /dir: surrounding spaces and quotes removed (Windows command-line
/// rules read /dir "C:\Games\BW\" as C:\Games\BW").
std::filesystem::path CleanFolder(std::wstring text)
{
    const auto isJunk = [](wchar_t c) { return c == L' ' || c == L'"' || c == L'\t'; };
    while (!text.empty() && isJunk(text.back())) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && isJunk(text[start])) {
        ++start;
    }
    text.erase(0, start);
    return text.empty() ? std::filesystem::path() : FullFolder(text);
}

Arguments ParseArguments()
{
    Arguments arguments;
    int count = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == nullptr) {
        return arguments;
    }
    for (int i = 1; i < count; ++i) {
        const std::wstring_view argument = argv[i];
        const bool hasValue = i + 1 < count;
        if (argument == L"/quiet") {
            arguments.quiet = true;
        }
        else if (argument == L"/uninstall") {
            arguments.uninstall = true;
        }
        else if (argument == L"/nostartmenu") {
            arguments.startMenu = false;
        }
        else if (argument == L"/desktop") {
            arguments.desktop = true;
        }
        else if (argument == L"/dir" && hasValue) {
            arguments.folder = CleanFolder(argv[++i]);
        }
        else if (argument == L"/wait" && hasValue) {
            arguments.waitFor = static_cast<DWORD>(wcstoul(argv[++i], nullptr, 10));
        }
        else if (argument == L"/elevated" && i + 3 < count) {
            arguments.elevatedAction = argv[++i];
            arguments.folder = FullFolder(argv[++i]);
            arguments.resultFile = argv[++i];
        }
    }
    LocalFree(argv);
    return arguments;
}

/// Messages: message boxes, or with /quiet the console setup was started from.
class Reporter {
public:
    explicit Reporter(bool quiet) : m_quiet(quiet) {}

    [[nodiscard]] bool Quiet() const noexcept { return m_quiet; }
    void SetOwner(HWND owner) noexcept { m_owner = owner; }

    void Error(const std::wstring& message) const { Show(message, MB_OK | MB_ICONERROR); }
    void Information(const std::wstring& message) const { Show(message, MB_OK | MB_ICONINFORMATION); }
    /// True for yes; always true when quiet.
    [[nodiscard]] bool Ask(const std::wstring& message, UINT icon = MB_ICONQUESTION) const
    {
        return m_quiet || MessageBoxW(m_owner, message.c_str(), Title(), MB_YESNO | icon) == IDYES;
    }
    void SetTitle(const wchar_t* title) noexcept { m_title = title; }

private:
    [[nodiscard]] const wchar_t* Title() const noexcept { return m_title; }

    void Show(const std::wstring& message, UINT type) const
    {
        if (!m_quiet) {
            MessageBoxW(m_owner, message.c_str(), Title(), type);
            return;
        }
        WriteConsoleLine(message);
    }

    static void WriteConsoleLine(const std::wstring& message)
    {
        HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
        if (output == nullptr || output == INVALID_HANDLE_VALUE) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
                return;
            }
            output = GetStdHandle(STD_ERROR_HANDLE);
        }
        const std::wstring line = message + L"\r\n";
        DWORD mode = 0;
        DWORD written = 0;
        if (GetConsoleMode(output, &mode)) {
            WriteConsoleW(output, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
        else {
            const std::string text = bw::Utf8(line);
            WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        }
    }

    bool m_quiet;
    HWND m_owner = nullptr;
    const wchar_t* m_title = kTitle;
};

std::wstring Join(const std::vector<std::wstring>& lines)
{
    std::wstring text;
    for (const std::wstring& line : lines) {
        text += (text.empty() ? L"" : L"\n") + line;
    }
    return text;
}

std::filesystem::path TemporaryFolder()
{
    std::wstring path(MAX_PATH + 1, L'\0');
    path.resize(GetTempPathW(static_cast<DWORD>(path.size()), path.data()));
    return path;
}

std::filesystem::path SystemFile(const wchar_t* name)
{
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size())));
    return std::filesystem::path(path) / name;
}

// ---------------------------------------------------------------------------------------------------------------
// File work, possibly in an elevated copy of this program

/// The result file of /elevated: UTF-8 text for the process that started it (an error, or the files kept).
void WriteResult(const std::filesystem::path& file, const std::wstring& text)
{
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << bw::Utf8(text);
}

std::wstring ReadResult(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return bw::Widen(text);
}

/// /elevated: the file work only. Registry entries and shortcuts belong to the user who started setup, so the
/// unelevated process makes them.
int RunElevatedAction(const Arguments& arguments)
{
    try {
        if (arguments.elevatedAction == L"install") {
            const Payload payload = Payload::Load();
            InstallFiles(arguments.folder, payload.Files(), bw::kVersion);
            WriteResult(arguments.resultFile, {});
            return kDone;
        }
        if (arguments.elevatedAction == L"uninstall") {
            WriteResult(arguments.resultFile, Join(RemoveFiles(arguments.folder).kept));
            return kDone;
        }
        WriteResult(arguments.resultFile, L"Unknown action.");
    }
    catch (const SetupError& error) {
        WriteResult(arguments.resultFile, error.Message());
    }
    catch (const std::exception& error) {
        WriteResult(arguments.resultFile, L"Unexpected error: " + bw::Widen(error.what()));
    }
    return kFailed;
}

/// Runs `action` in an elevated copy of this program (Windows asks the player) and returns its result text.
/// Throws SetupError when it fails or the player says no.
std::wstring RunElevated(std::wstring_view action, const std::filesystem::path& folder, HWND owner)
{
    const std::filesystem::path result =
        TemporaryFolder() / (L"BWDecompSetup-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()) + L".txt");
    const std::wstring parameters = L"/elevated " + std::wstring(action) + L" " + bw::QuoteArgument(folder.wstring()) +
                                    L" " + bw::QuoteArgument(result.wstring());
    const std::filesystem::path self = bw::ModulePath(nullptr);
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof execute;
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    execute.hwnd = owner;
    execute.lpVerb = L"runas";
    execute.lpFile = self.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute) || execute.hProcess == nullptr) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            throw SetupError(L"Administrator rights are needed to change " + folder.wstring() +
                             L", and they were not given. Nothing was changed.");
        }
        throw SetupError(L"Cannot start setup with administrator rights:\n" + WindowsErrorText(error));
    }
    const bw::UniqueHandle process(execute.hProcess);
    WaitForSingleObject(process.Get(), INFINITE);
    DWORD exitCode = kFailed;
    GetExitCodeProcess(process.Get(), &exitCode);
    std::wstring text = ReadResult(result);
    std::error_code ignored;
    std::filesystem::remove(result, ignored);
    if (exitCode != kDone) {
        throw SetupError(text.empty() ? L"Setup with administrator rights failed." : text);
    }
    return text;
}

void CheckNotRunning(const std::filesystem::path& folder)
{
    if (IsGameRunning(folder)) {
        throw SetupError(L"Black & White is running from " + folder.wstring() + L". Quit the game and try again.");
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Installing

struct InstallChoices {
    bool startMenu = true;
    bool desktop = false;
};

/// Installs into `folder`. Throws SetupError when the files cannot be installed; problems after that (registry,
/// shortcuts) are returned as warnings.
std::vector<std::wstring> Install(const std::filesystem::path& folder, const Payload& payload,
                                  const InstallChoices& choices, HWND owner)
{
    CheckNotRunning(folder);
    if (NeedsElevation(folder)) {
        RunElevated(L"install", folder, owner);
    }
    else {
        InstallFiles(folder, payload.Files(), bw::kVersion);
    }

    std::vector<std::wstring> warnings;
    auto attempt = [&](auto&& step) {
        try {
            step();
        }
        catch (const SetupError& error) {
            warnings.push_back(error.Message());
        }
    };
    attempt([&] { Register(folder, bw::kVersion, PayloadSize(payload.Files())); });
    attempt([&] { EnsureGameDir(folder); });
    if (choices.startMenu) {
        attempt([&] { CreateShortcut(ShortcutPlace::StartMenu, folder); });
    }
    if (choices.desktop) {
        attempt([&] { CreateShortcut(ShortcutPlace::Desktop, folder); });
    }
    return warnings;
}

std::wstring InstalledMessage(const std::filesystem::path& folder, const std::vector<std::wstring>& warnings)
{
    std::wstring message = L"Black & White Decompilation " + bw::Widen(bw::kVersion) + L" is installed in " +
                           folder.wstring() + L".";
    if (!warnings.empty()) {
        message += L"\n\nSome extras could not be set up:\n" + Join(warnings);
    }
    return message;
}

void StartGame(const std::filesystem::path& folder)
{
    const std::filesystem::path launcher = folder / kLauncherPath;
    ShellExecuteW(nullptr, L"open", launcher.c_str(), nullptr, folder.c_str(), SW_SHOWNORMAL);
}

struct DialogState {
    const Payload* payload = nullptr;
    const Arguments* arguments = nullptr;
    Reporter* reporter = nullptr;
    HFONT headingFont = nullptr;
    HICON statusIcon = nullptr;
    std::filesystem::path folder;
    bw::GameExecutableCheck check;
};

constexpr UINT_PTR kCheckTimer = 1;

std::wstring StatusText(const DialogState& state, PCWSTR& icon)
{
    icon = IDI_WARNING;
    if (state.folder.empty()) {
        icon = IDI_INFORMATION;
        return L"Choose the folder Black & White is installed in.";
    }
    std::wstring text;
    switch (state.check.build) {
    case bw::GameBuild::Missing:
        return L"There is no runblack.exe in this folder. Choose the folder Black & White is installed in.";
    case bw::GameBuild::Unreadable:
        return L"runblack.exe in this folder cannot be read.";
    case bw::GameBuild::Supported:
        icon = IDI_INFORMATION;
        text = L"Found Black & White 1.00 (English), the version the decompilation supports.";
        break;
    case bw::GameBuild::RetailSafeDisc:
        text = L"This runblack.exe is the SafeDisc-protected retail version, which current Windows cannot start. "
               L"Before playing, copy the unprotected runblack.exe from the disc's crack folder over it.";
        break;
    case bw::GameBuild::Unknown:
        text = L"This runblack.exe is not version 1.00 (English); BWLaunch will not start it. Official patches 1.1 "
               L"and 1.2 are not supported yet.";
        break;
    }
    if (const std::optional<Manifest> manifest = ReadManifest(state.folder)) {
        text += L"\n\nVersion " + (manifest->version.empty() ? std::wstring(L"?") : bw::Widen(manifest->version)) +
                L" is installed here and will be updated. Your BWDecomp.ini is kept.";
    }
    else if (std::error_code error; std::filesystem::exists(state.folder / L"BWDecomp.dll", error)) {
        text += L"\n\nThe copy of the decompilation in this folder will be replaced. Your BWDecomp.ini is kept.";
    }
    return text;
}

void UpdateStatus(HWND dialog, DialogState& state)
{
    std::wstring text(GetWindowTextLengthW(GetDlgItem(dialog, IDC_FOLDER)) + 1, L'\0');
    text.resize(GetDlgItemTextW(dialog, IDC_FOLDER, text.data(), static_cast<int>(text.size())));
    state.folder = CleanFolder(std::move(text));
    state.check = state.folder.empty() ? bw::GameExecutableCheck{}
                                       : bw::CheckGameExecutable(state.folder / L"runblack.exe");

    PCWSTR iconName = nullptr;
    SetDlgItemTextW(dialog, IDC_STATUS, StatusText(state, iconName).c_str());
    const HWND iconControl = GetDlgItem(dialog, IDC_STATUS_ICON);
    RECT bounds{};
    GetClientRect(iconControl, &bounds);
    HICON icon = nullptr;
    if (SUCCEEDED(LoadIconWithScaleDown(nullptr, iconName, bounds.right, bounds.bottom, &icon))) {
        SendMessageW(iconControl, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
        if (state.statusIcon != nullptr) {
            DestroyIcon(state.statusIcon);
        }
        state.statusIcon = icon;
    }

    const bool usable = state.check.build != bw::GameBuild::Missing && state.check.build != bw::GameBuild::Unreadable &&
                        !state.folder.empty();
    EnableWindow(GetDlgItem(dialog, IDOK), usable);
    std::error_code error;
    const bool update = usable && (ReadManifest(state.folder).has_value() ||
                                   std::filesystem::exists(state.folder / L"BWDecomp.dll", error));
    SetDlgItemTextW(dialog, IDOK, update ? L"&Update" : L"&Install");
}

void Browse(HWND dialog, const DialogState& state)
{
    ComPtr<IFileOpenDialog> picker;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker)))) {
        return;
    }
    DWORD options = 0;
    picker->GetOptions(&options);
    picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    picker->SetTitle(L"Black & White folder (the one with runblack.exe)");
    ComPtr<IShellItem> start;
    if (std::error_code error; !state.folder.empty() && std::filesystem::is_directory(state.folder, error) &&
                               SUCCEEDED(SHCreateItemFromParsingName(state.folder.c_str(), nullptr,
                                                                     IID_PPV_ARGS(&start)))) {
        picker->SetFolder(start.Get());
    }
    ComPtr<IShellItem> chosen;
    PWSTR path = nullptr;
    if (SUCCEEDED(picker->Show(dialog)) && SUCCEEDED(picker->GetResult(&chosen)) &&
        SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        SetDlgItemTextW(dialog, IDC_FOLDER, path);  // EN_CHANGE updates the status
        CoTaskMemFree(path);
    }
}

void OnInstall(HWND dialog, DialogState& state)
{
    KillTimer(dialog, kCheckTimer);
    UpdateStatus(dialog, state);
    if (!IsWindowEnabled(GetDlgItem(dialog, IDOK))) {
        return;
    }
    if (state.check.build != bw::GameBuild::Supported &&
        !state.reporter->Ask(bw::DescribeGameExecutable(state.check, state.folder / L"runblack.exe") +
                                 L"\n\nInstall anyway?",
                             MB_ICONWARNING)) {
        return;
    }
    const InstallChoices choices{IsDlgButtonChecked(dialog, IDC_STARTMENU) == BST_CHECKED,
                                 IsDlgButtonChecked(dialog, IDC_DESKTOP) == BST_CHECKED};
    EnableWindow(dialog, FALSE);
    const HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    std::optional<std::vector<std::wstring>> warnings;
    try {
        warnings = Install(state.folder, *state.payload, choices, dialog);
    }
    catch (const SetupError& error) {
        SetCursor(previous);
        EnableWindow(dialog, TRUE);
        state.reporter->Error(error.Message());
        return;
    }
    SetCursor(previous);
    EnableWindow(dialog, TRUE);
    std::wstring message = InstalledMessage(state.folder, *warnings);
    if (state.check.build == bw::GameBuild::Supported) {
        if (state.reporter->Ask(message + L"\n\nStart Black & White now?", MB_ICONINFORMATION)) {
            StartGame(state.folder);
        }
    }
    else {
        state.reporter->Information(message);
    }
    EndDialog(dialog, kDone);
}

void InitDialog(HWND dialog, DialogState& state)
{
    SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(&state));
    state.reporter->SetOwner(dialog);

    // The heading: the dialog font, larger and bold.
    LOGFONTW font{};
    if (GetObjectW(reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0)), sizeof font, &font) != 0) {
        font.lfHeight = font.lfHeight * 3 / 2;
        font.lfWeight = FW_SEMIBOLD;
        state.headingFont = CreateFontIndirectW(&font);
        SendDlgItemMessageW(dialog, IDC_HEADING, WM_SETFONT, reinterpret_cast<WPARAM>(state.headingFont), FALSE);
    }
    SetDlgItemTextW(dialog, IDC_HEADING, (L"Black && White Decompilation " + bw::Widen(bw::kVersion)).c_str());

    const std::filesystem::path folder =
        state.arguments->folder.empty() ? FindGameFolder() : state.arguments->folder;
    SetDlgItemTextW(dialog, IDC_FOLDER, folder.c_str());
    SHAutoComplete(GetDlgItem(dialog, IDC_FOLDER), SHACF_FILESYS_DIRS);
    CheckDlgButton(dialog, IDC_STARTMENU, state.arguments->startMenu ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_DESKTOP, state.arguments->desktop ? BST_CHECKED : BST_UNCHECKED);
    KillTimer(dialog, kCheckTimer);  // set by EN_CHANGE above
    UpdateStatus(dialog, state);
    SetFocus(GetDlgItem(dialog, IsWindowEnabled(GetDlgItem(dialog, IDOK)) ? IDOK : IDC_FOLDER));
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    try {
        switch (message) {
        case WM_INITDIALOG:
            InitDialog(dialog, *reinterpret_cast<DialogState*>(lParam));
            return FALSE;  // focus set by InitDialog
        case WM_COMMAND:
            if (state == nullptr) {
                break;
            }
            switch (LOWORD(wParam)) {
            case IDC_FOLDER:
                if (HIWORD(wParam) == EN_CHANGE) {
                    SetTimer(dialog, kCheckTimer, 300, nullptr);  // check once typing pauses
                }
                return TRUE;
            case IDC_BROWSE:
                Browse(dialog, *state);
                return TRUE;
            case IDOK:
                OnInstall(dialog, *state);
                return TRUE;
            case IDCANCEL:
                EndDialog(dialog, kCancelled);
                return TRUE;
            default:
                break;
            }
            break;
        case WM_TIMER:
            if (state != nullptr && wParam == kCheckTimer) {
                KillTimer(dialog, kCheckTimer);
                UpdateStatus(dialog, *state);
                return TRUE;
            }
            break;
        case WM_DESTROY:
            if (state != nullptr) {
                DeleteObject(state->headingFont);
                if (state->statusIcon != nullptr) {
                    DestroyIcon(state->statusIcon);
                }
            }
            break;
        default:
            break;
        }
    }
    catch (const std::exception& error) {
        MessageBoxW(dialog, (L"Unexpected error: " + bw::Widen(error.what())).c_str(), kTitle, MB_OK | MB_ICONERROR);
    }
    return FALSE;
}

int RunInstall(const Arguments& arguments, Reporter& reporter)
{
    const Payload payload = Payload::Load();
    if (!reporter.Quiet()) {
        DialogState state{&payload, &arguments, &reporter};
        const INT_PTR result =
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETUP), nullptr, &DialogProc,
                            reinterpret_cast<LPARAM>(&state));
        return result == kDone ? kDone : result == -1 ? kFailed : kCancelled;
    }

    const std::filesystem::path folder = arguments.folder.empty() ? FindGameFolder() : arguments.folder;
    if (folder.empty()) {
        reporter.Error(L"Cannot find Black & White. Name its folder with /dir.");
        return kFailed;
    }
    if (const bw::GameExecutableCheck check = bw::CheckGameExecutable(folder / L"runblack.exe");
        check.build == bw::GameBuild::Missing) {
        reporter.Error(L"There is no runblack.exe in " + folder.wstring() + L".");
        return kFailed;
    }
    const std::vector<std::wstring> warnings =
        Install(folder, payload, {arguments.startMenu, arguments.desktop}, nullptr);
    reporter.Information(InstalledMessage(folder, warnings));
    return kDone;
}

// ---------------------------------------------------------------------------------------------------------------
// Uninstalling

std::optional<std::filesystem::path> InstalledFolder(const Arguments& arguments)
{
    if (!arguments.folder.empty()) {
        return arguments.folder;
    }
    // Uninstall.exe in <game>\BWDecomp.
    const std::filesystem::path own = bw::ModuleDirectory(nullptr);
    if (SamePath(own.filename(), kDataFolder) && ReadManifest(own.parent_path())) {
        return own.parent_path();
    }
    return RegisteredFolder();
}

bool IsInside(const std::filesystem::path& file, const std::filesystem::path& folder)
{
    for (std::filesystem::path parent = file.parent_path(); !parent.empty() && parent != parent.root_path();
         parent = parent.parent_path()) {
        if (SamePath(parent, folder)) {
            return true;
        }
    }
    return false;
}

/// The uninstaller cannot delete itself while it runs, so it continues from a copy in %TEMP%.
int ContinueFromTemporaryCopy(const Arguments& arguments, const std::filesystem::path& folder)
{
    const std::filesystem::path copyFolder =
        TemporaryFolder() / (kTemporaryUninstallerPrefix + std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64()));
    const std::filesystem::path copy = copyFolder / L"Uninstall.exe";
    std::error_code error;
    std::filesystem::create_directories(copyFolder, error);
    if (error || !CopyFileW(bw::ModulePath(nullptr).c_str(), copy.c_str(), FALSE)) {
        throw SetupError(L"Cannot copy the uninstaller to " + copyFolder.wstring() + L".");
    }
    std::wstring commandLine = bw::QuoteArgument(copy.wstring()) + L" /uninstall /dir " +
                               bw::QuoteArgument(folder.wstring()) + L" /wait " +
                               std::to_wstring(GetCurrentProcessId()) + (arguments.quiet ? L" /quiet" : L"");
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION started{};
    if (!CreateProcessW(copy.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, copyFolder.c_str(),
                        &startup, &started)) {
        throw SetupError(L"Cannot start the uninstaller:\n" + WindowsErrorText(GetLastError()));
    }
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    return kDone;
}

/// The copy in %TEMP% deletes its folder once it has exited.
void DeleteTemporaryCopyLater()
{
    const std::filesystem::path own = bw::ModuleDirectory(nullptr);
    if (!own.filename().wstring().starts_with(kTemporaryUninstallerPrefix) || !IsInside(own / L"x", TemporaryFolder())) {
        return;
    }
    const std::filesystem::path cmd = SystemFile(L"cmd.exe");
    std::wstring commandLine = bw::QuoteArgument(cmd.wstring()) + L" /d /c \"" +
                               bw::QuoteArgument(SystemFile(L"ping.exe").wstring()) + L" -n 3 127.0.0.1 > nul & rd /s /q " +
                               bw::QuoteArgument(own.wstring()) + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION started{};
    if (CreateProcessW(cmd.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       TemporaryFolder().c_str(), &startup, &started)) {
        CloseHandle(started.hThread);
        CloseHandle(started.hProcess);
    }
}

int RunUninstall(const Arguments& arguments, Reporter& reporter)
{
    reporter.SetTitle(kUninstallTitle);
    const std::optional<std::filesystem::path> folder = InstalledFolder(arguments);
    if (!folder) {
        reporter.Error(L"The decompilation is not installed.");
        return kFailed;
    }
    if (IsInside(bw::ModulePath(nullptr), *folder)) {
        return ContinueFromTemporaryCopy(arguments, *folder);
    }
    if (arguments.waitFor != 0) {
        const bw::UniqueHandle original(OpenProcess(SYNCHRONIZE, FALSE, arguments.waitFor));
        if (original) {
            WaitForSingleObject(original.Get(), 10'000);
        }
    }

    int result = kCancelled;
    if (reporter.Ask(L"Remove the Black & White decompilation from " + folder->wstring() +
                     L"?\n\nThe game, your saved games, screenshots and texture packs are not touched.")) {
        try {
            CheckNotRunning(*folder);
            std::vector<std::wstring> kept;
            if (NeedsElevation(*folder)) {
                const std::wstring text = RunElevated(L"uninstall", *folder, nullptr);
                if (!text.empty()) {
                    kept.push_back(text);
                }
            }
            else {
                kept = RemoveFiles(*folder).kept;
            }
            RemoveShortcuts(*folder);
            if (const std::optional<std::filesystem::path> registered = RegisteredFolder();
                !registered || SamePath(*registered, *folder)) {
                Unregister();
            }
            std::wstring message = L"The decompilation was removed from " + folder->wstring() + L".";
            if (!kept.empty()) {
                message += L"\n\nKept because you changed them:\n" + Join(kept);
            }
            reporter.Information(message);
            result = kDone;
        }
        catch (const SetupError& error) {
            reporter.Error(error.Message());
            result = kFailed;
        }
    }
    DeleteTemporaryCopyLater();
    return result;
}

int Run(const Arguments& arguments, Reporter& reporter)
{
    if (!arguments.elevatedAction.empty()) {
        return RunElevatedAction(arguments);
    }
    return arguments.uninstall ? RunUninstall(arguments, reporter) : RunInstall(arguments, reporter);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int result = kFailed;
    Reporter reporter(false);
    try {
        const Arguments arguments = ParseArguments();
        reporter = Reporter(arguments.quiet);
        result = Run(arguments, reporter);
    }
    catch (const SetupError& error) {
        reporter.Error(error.Message());
    }
    catch (const std::exception& error) {
        reporter.Error(L"Unexpected error: " + bw::Widen(error.what()));
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return result;
}
