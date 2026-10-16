#include "setup/Install.h"

#include "core/GameExecutable.h"
#include "core/Handle.h"
#include "core/Image.h"
#include "core/Unicode.h"
#include "core/Windows.h"

#include <algorithm>
#include <fstream>

namespace bw::setup {

namespace {

constexpr std::string_view kManifestHeader = "# Black & White decompilation ";
constexpr std::uintmax_t kLargestInstalledFile = 256u << 20;

const ManifestEntry* Find(const std::vector<ManifestEntry>& entries, std::wstring_view path) noexcept
{
    const auto it = std::ranges::find_if(entries, [&](const ManifestEntry& entry) { return SamePath(entry.path, path); });
    return it != entries.end() ? &*it : nullptr;
}

bool IsHexDigest(std::string_view text) noexcept
{
    return text.size() == 64 &&
           std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool FileMatches(const std::filesystem::path& file, const std::string& sha256)
{
    const std::optional<std::string> actual = FileSha256(file, kLargestInstalledFile);
    return actual && *actual == sha256;
}

std::wstring FailureText(std::wstring_view what, const std::filesystem::path& path, DWORD error)
{
    return std::wstring(what) + L" " + path.wstring() + L":\n" + WindowsErrorText(error);
}

/// Writes `data` next to `target` and moves it over `target`.
void WriteReplacing(const std::filesystem::path& target, std::span<const std::byte> data)
{
    std::filesystem::path temporary = target;
    temporary += L".setup-new";
    {
        const UniqueHandle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            throw SetupError(FailureText(L"Cannot create", temporary, GetLastError()));
        }
        std::span<const std::byte> rest = data;
        while (!rest.empty()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(rest.size(), 1u << 20));
            DWORD written = 0;
            if (!WriteFile(file.Get(), rest.data(), chunk, &written, nullptr) || written != chunk) {
                const DWORD error = GetLastError();
                DeleteFileW(temporary.c_str());
                throw SetupError(FailureText(L"Cannot write", temporary, error));
            }
            rest = rest.subspan(chunk);
        }
        if (!FlushFileBuffers(file.Get())) {
            const DWORD error = GetLastError();
            DeleteFileW(temporary.c_str());
            throw SetupError(FailureText(L"Cannot write", temporary, error));
        }
    }
    // A read-only target cannot be replaced.
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(target.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        throw SetupError(FailureText(L"Cannot replace", target, error));
    }
}

void WriteManifest(const std::filesystem::path& folder, const Manifest& manifest)
{
    std::string text = std::string(kManifestHeader) + manifest.version + "\r\n";
    text += "# Files setup installed: SHA-256, R (replaced on update) or K (kept when changed), path.\r\n";
    for (const ManifestEntry& entry : manifest.entries) {
        text += entry.sha256;
        text += entry.existing == Existing::Keep ? " K " : " R ";
        text += Utf8(entry.path);
        text += "\r\n";
    }
    const std::filesystem::path path = folder / kManifestPath;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    WriteReplacing(path, std::as_bytes(std::span(text)));
}

/// Deletes `file`; false (with the error) when it exists and cannot be deleted.
bool Delete(const std::filesystem::path& file, DWORD& error) noexcept
{
    const DWORD attributes = GetFileAttributesW(file.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(file.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }
    if (DeleteFileW(file.c_str())) {
        return true;
    }
    error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

/// Removes the now empty folders between `folder` and the files of `entries`, deepest first.
void RemoveEmptyFolders(const std::filesystem::path& folder, const std::vector<ManifestEntry>& entries)
{
    std::vector<std::filesystem::path> folders{folder / kDataFolder};
    for (const ManifestEntry& entry : entries) {
        for (std::filesystem::path parent = std::filesystem::path(entry.path).parent_path(); !parent.empty();
             parent = parent.parent_path()) {
            folders.push_back(folder / parent);
        }
    }
    std::ranges::sort(folders, [](const auto& a, const auto& b) { return a.native().size() > b.native().size(); });
    for (const std::filesystem::path& path : folders) {
        RemoveDirectoryW(path.c_str());  // fails, as intended, unless empty
    }
}

}  // namespace

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) noexcept
{
    try {
        // A trailing separator does not change the folder ("C:\Games\" is "C:\Games"; "C:\" stays).
        const auto normal = [](const std::filesystem::path& path) {
            std::wstring text = path.lexically_normal().native();
            while (text.size() > 1 && text.back() == L'\\' && text[text.size() - 2] != L':') {
                text.pop_back();
            }
            return text;
        };
        const std::wstring left = normal(a);
        const std::wstring right = normal(b);
        return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                    static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
    }
    catch (const std::exception&) {
        return false;
    }
}

std::wstring WindowsErrorText(unsigned long error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = length != 0 ? std::wstring(buffer, length) : L"Windows error";
    LocalFree(buffer);
            while (text.size() > 1 && text.back() == L'\\' && text[text.size() - 2] != L':') {
        text.pop_back();
    }
    return text + L" (" + std::to_wstring(error) + L")";
}

bool IsSafeRelativePath(std::wstring_view path) noexcept
{
    if (path.empty() || path.size() >= MAX_PATH) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find(L'\\', start), path.size());
        const std::wstring_view part = path.substr(start, end - start);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ') {
            return false;  // also rejects a leading "\" (rooted) and "\\" (UNC)
        }
        for (const wchar_t c : part) {
            if (c < 0x20 || std::wstring_view(L"<>:\"/|?*").find(c) != std::wstring_view::npos) {
                return false;  // ':' also rules out drives and alternate data streams
            }
        }
        start = end + 1;
    }
    return true;
}

std::string Sha256Hex(std::span<const std::byte> data)
{
    const std::optional<image::Sha256> digest = image::ComputeSha256(data);
    if (!digest) {
        throw SetupError(L"Cannot compute a SHA-256 hash (bcrypt).");
    }
    const auto hex = image::ToHex(*digest);
    return std::string(hex.data(), hex.size());
}

std::optional<Manifest> ReadManifest(const std::filesystem::path& folder)
{
    std::ifstream stream(folder / kManifestPath, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    Manifest manifest;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string_view text = line;
        if (text.starts_with(kManifestHeader)) {
            manifest.version = text.substr(kManifestHeader.size());
            continue;
        }
        // "<64 hex digits> <R|K> <path>"
        if (text.size() < 68 || !IsHexDigest(text.substr(0, 64)) || text[64] != ' ' || text[66] != ' ' ||
            (text[65] != 'R' && text[65] != 'K')) {
            continue;
        }
        std::wstring path = Widen(text.substr(67));
        if (!IsSafeRelativePath(path)) {
            continue;
        }
        manifest.entries.push_back(
            {std::move(path), std::string(text.substr(0, 64)), text[65] == 'K' ? Existing::Keep : Existing::Replace});
    }
    return manifest;
}

void InstallFiles(const std::filesystem::path& folder, std::span<const PayloadFile> files, std::string_view version)
{
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        throw SetupError(L"The folder " + folder.wstring() + L" does not exist.");
    }
    const Manifest previous = ReadManifest(folder).value_or(Manifest{});
    Manifest manifest{std::string(version), {}};

    // On failure the manifest still lists every file this or the previous version wrote.
    auto keepPreviousEntries = [&] {
        for (const ManifestEntry& entry : previous.entries) {
            if (Find(manifest.entries, entry.path) == nullptr) {
                manifest.entries.push_back(entry);
            }
        }
    };
    try {
        for (const PayloadFile& file : files) {
            if (!IsSafeRelativePath(file.path)) {
                throw SetupError(L"Invalid file name in setup: " + file.path);
            }
            const std::filesystem::path target = folder / file.path;
            if (file.existing == Existing::Keep && std::filesystem::exists(target, error)) {
                if (const ManifestEntry* entry = Find(previous.entries, file.path)) {
                    manifest.entries.push_back(*entry);
                }
                continue;
            }
            std::filesystem::create_directories(target.parent_path(), error);
            if (error) {
                throw SetupError(L"Cannot create the folder " + target.parent_path().wstring() + L":\n" +
                                 Widen(error.message()));
            }
            WriteReplacing(target, file.data);
            manifest.entries.push_back({file.path, Sha256Hex(file.data), file.existing});
        }

        // Files an earlier version installed and this one does not.
        for (const ManifestEntry& entry : previous.entries) {
            const bool installed = std::ranges::any_of(files, [&](const PayloadFile& file) {
                return SamePath(file.path, entry.path);
            });
            if (installed) {
                continue;
            }
            const std::filesystem::path target = folder / entry.path;
            DWORD deleteError = 0;
            if ((entry.existing == Existing::Keep && !FileMatches(target, entry.sha256)) ||
                !Delete(target, deleteError)) {
                manifest.entries.push_back(entry);
            }
        }
    }
    catch (const SetupError&) {
        keepPreviousEntries();
        try {
            WriteManifest(folder, manifest);
        }
        catch (const SetupError&) {
        }
        throw;
    }
    WriteManifest(folder, manifest);
}

RemoveResult RemoveFiles(const std::filesystem::path& folder)
{
    const std::optional<Manifest> manifest = ReadManifest(folder);
    if (!manifest) {
        throw SetupError(L"The decompilation is not installed in " + folder.wstring() + L" (there is no " +
                         std::wstring(kManifestPath) + L").");
    }
    RemoveResult result;
    for (std::size_t i = 0; i < manifest->entries.size(); ++i) {
        const ManifestEntry& entry = manifest->entries[i];
        const std::filesystem::path target = folder / entry.path;
        std::error_code error;
        if (!std::filesystem::exists(target, error)) {
            continue;
        }
        if (entry.existing == Existing::Keep && !FileMatches(target, entry.sha256)) {
            result.kept.push_back(entry.path);
            continue;
        }
        DWORD deleteError = 0;
        if (!Delete(target, deleteError)) {
            // Leave a manifest listing what is still there, so removal can be retried.
            Manifest remaining{manifest->version, {manifest->entries.begin() + static_cast<std::ptrdiff_t>(i),
                                                   manifest->entries.end()}};
            try {
                WriteManifest(folder, remaining);
            }
            catch (const SetupError&) {
            }
            throw SetupError(FailureText(L"Cannot delete", target, deleteError));
        }
    }
    for (const std::wstring_view generated : kGeneratedFiles) {
        DWORD ignored = 0;
        (void)Delete(folder / generated, ignored);
    }
    DWORD error = 0;
    if (!Delete(folder / kManifestPath, error)) {
        throw SetupError(FailureText(L"Cannot delete", folder / kManifestPath, error));
    }
    RemoveEmptyFolders(folder, manifest->entries);
    return result;
}

std::uint64_t PayloadSize(std::span<const PayloadFile> files) noexcept
{
    std::uint64_t size = 0;
    for (const PayloadFile& file : files) {
        size += file.data.size();
    }
    return size;
}

}  // namespace bw::setup
