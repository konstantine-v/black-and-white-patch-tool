#include "setup/System.h"

#include "core/CommandLine.h"
#include "core/Handle.h"
#include "core/Module.h"
#include "core/Windows.h"
#include "setup/Install.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <array>

namespace bw::setup {

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlackWhiteDecomp";
constexpr wchar_t kGameKey[] = L"Software\\Lionhead Studios Ltd\\Black & White";
constexpr wchar_t kShortcutName[] = L"Black & White (Decompilation).lnk";

std::optional<std::wstring> ReadString(HKEY root, const wchar_t* key, const wchar_t* value)
{
    DWORD size = 0;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
        return std::nullopt;
    }
    std::wstring text(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, text.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    text.resize(wcsnlen(text.c_str(), text.size()));
    return text;
}

class RegistryKey {
public:
    RegistryKey(HKEY root, const wchar_t* path)
    {
        const LSTATUS status =
            RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &m_key, nullptr);
        if (status != ERROR_SUCCESS) {
            throw SetupError(L"Cannot write the registry key HKEY_CURRENT_USER\\" + std::wstring(path) + L":\n" +
                             WindowsErrorText(static_cast<unsigned long>(status)));
        }
    }
    ~RegistryKey() { RegCloseKey(m_key); }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    void Set(const wchar_t* name, const std::wstring& text) const
    {
        Check(RegSetValueExW(m_key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                             static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))));
    }
    void Set(const wchar_t* name, DWORD number) const
    {
        Check(RegSetValueExW(m_key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&number), sizeof number));
    }

private:
    static void Check(LSTATUS status)
    {
        if (status != ERROR_SUCCESS) {
            throw SetupError(L"Cannot write to the registry:\n" + WindowsErrorText(static_cast<unsigned long>(status)));
        }
    }

    HKEY m_key = nullptr;
};

std::filesystem::path KnownFolder(const KNOWNFOLDERID& id)
{
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) {
        CoTaskMemFree(path);
        return {};
    }
    std::filesystem::path result = path;
    CoTaskMemFree(path);
    return result;
}

std::filesystem::path ShortcutPath(ShortcutPlace place)
{
    const std::filesystem::path folder =
        KnownFolder(place == ShortcutPlace::StartMenu ? FOLDERID_Programs : FOLDERID_Desktop);
    return folder.empty() ? folder : folder / kShortcutName;
}

bool HoldsGame(const std::filesystem::path& folder)
{
    std::error_code error;
    return !folder.empty() && std::filesystem::is_regular_file(folder / L"runblack.exe", error);
}

/// False when this process may not write in `path`: a folder (by creating a file) or an existing file (by opening it
/// for replacement).
bool CanWrite(const std::filesystem::path& path)
{
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        const std::filesystem::path probe = path / (L".bwsetup-" + std::to_wstring(GetCurrentProcessId()));
        const UniqueHandle file(CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
        return file || GetLastError() != ERROR_ACCESS_DENIED;
    }
    if (std::filesystem::exists(path, error)) {
        const UniqueHandle file(CreateFileW(path.c_str(), DELETE | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        return file || GetLastError() != ERROR_ACCESS_DENIED;
    }
    return true;
}

bool IsElevated()
{
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        return false;
    }
    const UniqueHandle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.Get(), TokenElevation, &elevation, sizeof elevation, &size) &&
           elevation.TokenIsElevated != 0;
}

}  // namespace

std::optional<std::filesystem::path> RegisteredFolder()
{
    if (std::optional<std::wstring> folder = ReadString(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation")) {
        return std::filesystem::path(std::move(*folder));
    }
    return std::nullopt;
}

std::filesystem::path FindGameFolder()
{
    std::vector<std::filesystem::path> candidates;
    if (std::optional<std::filesystem::path> registered = RegisteredFolder()) {
        candidates.push_back(std::move(*registered));
    }
    for (const HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        if (std::optional<std::wstring> gameDir = ReadString(root, kGameKey, L"GameDir")) {
            candidates.emplace_back(std::move(*gameDir));
        }
    }
    const std::filesystem::path setupFolder = ModuleDirectory(nullptr);
    candidates.push_back(setupFolder);
    candidates.push_back(setupFolder.parent_path());  // BWDecomp\Uninstall.exe
    for (const KNOWNFOLDERID* programs : {&FOLDERID_ProgramFilesX86, &FOLDERID_ProgramFiles}) {
        if (const std::filesystem::path folder = KnownFolder(*programs); !folder.empty()) {
            candidates.push_back(folder / L"Lionhead Studios Ltd" / L"Black & White");
        }
    }
    for (const std::filesystem::path& candidate : candidates) {
        if (HoldsGame(candidate)) {
            return candidate.lexically_normal();
        }
    }
    return {};
}

void Register(const std::filesystem::path& folder, std::string_view version, std::uint64_t sizeBytes)
{
    const RegistryKey key(HKEY_CURRENT_USER, kUninstallKey);
    const std::wstring uninstaller = QuoteArgument((folder / kUninstallerPath).wstring());
    key.Set(L"DisplayName", std::wstring(kProductName));
    key.Set(L"DisplayVersion", std::wstring(version.begin(), version.end()));
    key.Set(L"DisplayIcon", (folder / L"runblack.exe").wstring() + L",0");
    key.Set(L"InstallLocation", folder.wstring());
    key.Set(L"UninstallString", uninstaller + L" /uninstall");
    key.Set(L"QuietUninstallString", uninstaller + L" /uninstall /quiet");
    key.Set(L"Comments", std::wstring(L"Fixes and decompiled code for Black & White 1.00, started by BWLaunch.exe"));
    SYSTEMTIME today{};
    GetLocalTime(&today);
    std::array<wchar_t, 9> date{};
    swprintf_s(date.data(), date.size(), L"%04u%02u%02u", today.wYear, today.wMonth, today.wDay);
    key.Set(L"InstallDate", std::wstring(date.data()));
    key.Set(L"EstimatedSize", static_cast<DWORD>((sizeBytes + 1023) / 1024));
    key.Set(L"NoModify", DWORD{1});
    key.Set(L"NoRepair", DWORD{1});
}

void Unregister()
{
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
}

void EnsureGameDir(const std::filesystem::path& folder)
{
    if (ReadString(HKEY_CURRENT_USER, kGameKey, L"GameDir")) {
        return;
    }
    const RegistryKey key(HKEY_CURRENT_USER, kGameKey);
    key.Set(L"GameDir", folder.wstring());
}

void CreateShortcut(ShortcutPlace place, const std::filesystem::path& folder)
{
    const std::filesystem::path path = ShortcutPath(place);
    if (path.empty()) {
        throw SetupError(L"Cannot find the folder for the shortcut.");
    }
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    const std::filesystem::path launcher = folder / kLauncherPath;
    const std::filesystem::path game = folder / L"runblack.exe";
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(result)) {
        link->SetPath(launcher.c_str());
        link->SetWorkingDirectory(folder.c_str());
        link->SetDescription(L"Black & White with the decompilation's fixes");
        link->SetIconLocation(game.c_str(), 0);
        result = link.As(&file);
    }
    if (SUCCEEDED(result)) {
        result = file->Save(path.c_str(), TRUE);
    }
    if (FAILED(result)) {
        throw SetupError(L"Cannot create the shortcut " + path.wstring() + L":\n" +
                         WindowsErrorText(static_cast<unsigned long>(result)));
    }
}

void RemoveShortcuts(const std::filesystem::path& folder)
{
    const std::filesystem::path launcher = folder / kLauncherPath;
    for (const ShortcutPlace place : {ShortcutPlace::StartMenu, ShortcutPlace::Desktop}) {
        const std::filesystem::path path = ShortcutPath(place);
        std::error_code error;
        if (path.empty() || !std::filesystem::exists(path, error)) {
            continue;
        }
        ComPtr<IShellLinkW> link;
        ComPtr<IPersistFile> file;
        std::wstring target(32768, L'\0');
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
            FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(), STGM_READ)) ||
            FAILED(link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH))) {
            continue;
        }
        target.resize(wcsnlen(target.c_str(), target.size()));
        if (SamePath(target, launcher)) {
            DeleteFileW(path.c_str());
        }
    }
}

bool IsGameRunning(const std::filesystem::path& folder)
{
    const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Process32FirstW(snapshot.Get(), &entry); more; more = Process32NextW(snapshot.Get(), &entry)) {
        if (_wcsicmp(entry.szExeFile, L"runblack.exe") != 0 && _wcsicmp(entry.szExeFile, L"BWLaunch.exe") != 0) {
            continue;
        }
        const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process) {
            continue;
        }
        std::wstring image(32768, L'\0');
        DWORD size = static_cast<DWORD>(image.size());
        if (QueryFullProcessImageNameW(process.Get(), 0, image.data(), &size)) {
            image.resize(size);
            if (SamePath(std::filesystem::path(image).parent_path(), folder)) {
                return true;
            }
        }
    }
    return false;
}

bool NeedsElevation(const std::filesystem::path& folder)
{
    if (IsElevated()) {
        return false;
    }
    for (const std::filesystem::path& path : {folder, folder / kDataFolder, folder / kLauncherPath,
                                              folder / L"BWDecomp.dll", folder / kUninstallerPath}) {
        if (!CanWrite(path)) {
            return true;
        }
    }
    return false;
}

}  // namespace bw::setup
