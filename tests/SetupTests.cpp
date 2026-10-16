#include "Test.h"

#include "core/GameExecutable.h"
#include "core/Unicode.h"
#include "core/Windows.h"
#include "setup/Install.h"
#include "setup/System.h"

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace bw::setup;

/// A fresh, empty folder below %TEMP%, removed again by the destructor.
class TemporaryFolder {
public:
    explicit TemporaryFolder(std::wstring_view name)
        : m_path(std::filesystem::temp_directory_path() /
                 (std::wstring(name) + L"-" + std::to_wstring(GetCurrentProcessId())))
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

std::span<const std::byte> Bytes(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

std::string ReadText(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void WriteText(const std::filesystem::path& file, std::string_view text)
{
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

bool Exists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error);
}

}  // namespace

TEST(SetupAcceptsOnlyPathsInsideTheFolder)
{
    CHECK(IsSafeRelativePath(L"BWLaunch.exe"));
    CHECK(IsSafeRelativePath(L"BWDecomp\\README.txt"));
    CHECK(IsSafeRelativePath(L"a\\b\\c.md"));
    for (const std::wstring_view bad : {L"", L"..", L"..\\runblack.exe", L"BWDecomp\\..\\..\\x", L"\\Windows\\x.dll",
                                        L"\\\\server\\share\\x", L"C:\\x.dll", L"C:x.dll", L"a/b", L"x.dll:stream",
                                        L"a\\\\b", L"a\\", L".\\a", L"a.", L"a ", L"a\\b?", L"con\x01"}) {
        CHECK(!IsSafeRelativePath(bad));
    }
}

TEST(SetupComparesPathsLikeWindows)
{
    CHECK(SamePath(L"C:\\Games\\Black & White", L"c:\\games\\BLACK & WHITE\\"));
    CHECK(SamePath(L"C:\\Games\\x\\..\\Black & White", L"C:\\Games\\Black & White"));
    CHECK(!SamePath(L"C:\\Games\\Black & White", L"C:\\Games\\Black"));
}

TEST(GameExecutablesAreRecognised)
{
    const TemporaryFolder folder(L"bw_tests_game");
    CHECK(bw::CheckGameExecutable(folder.Path() / L"runblack.exe").build == bw::GameBuild::Missing);
    WriteText(folder.Path() / L"runblack.exe", "abc");
    const bw::GameExecutableCheck check = bw::CheckGameExecutable(folder.Path() / L"runblack.exe");
    CHECK(check.build == bw::GameBuild::Unknown);
    CHECK_EQ(check.sha256, std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(!bw::DescribeGameExecutable(check, folder.Path() / L"runblack.exe").empty());
    CHECK(!bw::FileSha256(folder.Path() / L"runblack.exe", 2).has_value());  // larger than allowed
}

TEST(SetupInstallsUpdatesAndRemoves)
{
    const TemporaryFolder folder(L"bw_tests_setup");
    const std::filesystem::path game = folder.Path();

    // Version 1: a program file, settings, a document that version 2 drops.
    const std::vector<PayloadFile> first = {
        {L"BWLaunch.exe", Bytes("launcher 1"), Existing::Replace},
        {L"BWDecomp.ini", Bytes("settings 1"), Existing::Keep},
        {L"BWDecomp\\OLD.md", Bytes("old"), Existing::Replace},
        {L"BWDecomp\\Docs\\README.txt", Bytes("readme 1"), Existing::Replace},
    };
    InstallFiles(game, first, "1.0.0");
    CHECK_EQ(ReadText(game / L"BWLaunch.exe"), std::string("launcher 1"));
    CHECK_EQ(ReadText(game / L"BWDecomp.ini"), std::string("settings 1"));
    const std::optional<Manifest> manifest = ReadManifest(game);
    CHECK(manifest.has_value());
    if (manifest) {
        CHECK_EQ(manifest->version, std::string("1.0.0"));
        CHECK_EQ(manifest->entries.size(), first.size());
    }

    // Version 2 replaces the program, keeps the settings and removes the dropped document.
    const std::vector<PayloadFile> second = {
        {L"BWLaunch.exe", Bytes("launcher 2"), Existing::Replace},
        {L"BWDecomp.ini", Bytes("settings 2"), Existing::Keep},
        {L"BWDecomp\\Docs\\README.txt", Bytes("readme 2"), Existing::Replace},
    };
    InstallFiles(game, second, "2.0.0");
    CHECK_EQ(ReadText(game / L"BWLaunch.exe"), std::string("launcher 2"));
    CHECK_EQ(ReadText(game / L"BWDecomp.ini"), std::string("settings 1"));
    CHECK(!Exists(game / L"BWDecomp\\OLD.md"));
    CHECK(!Exists(game / L"BWLaunch.exe.setup-new"));
    CHECK_EQ(ReadManifest(game).value_or(Manifest{}).version, std::string("2.0.0"));

    // Unchanged settings and everything else go, with the log the DLL wrote; files setup did not write stay.
    WriteText(game / L"BWDecomp.log", "log");
    WriteText(game / L"runblack.exe", "game");
    RemoveResult removed = RemoveFiles(game);
    CHECK(removed.kept.empty());
    CHECK(!Exists(game / L"BWLaunch.exe"));
    CHECK(!Exists(game / L"BWDecomp.ini"));
    CHECK(!Exists(game / L"BWDecomp.log"));
    CHECK(!Exists(game / kDataFolder));
    CHECK(Exists(game / L"runblack.exe"));

    // Changed settings are kept.
    InstallFiles(game, second, "2.0.0");
    WriteText(game / L"BWDecomp.ini", "my settings");
    removed = RemoveFiles(game);
    CHECK_EQ(removed.kept.size(), std::size_t{1});
    CHECK_EQ(ReadText(game / L"BWDecomp.ini"), std::string("my settings"));
    CHECK(!Exists(game / kManifestPath));

    bool threw = false;
    try {
        (void)RemoveFiles(game);
    }
    catch (const SetupError&) {
        threw = true;  // nothing installed
    }
    CHECK(threw);
}

TEST(SetupIgnoresManifestPathsOutsideTheFolder)
{
    const TemporaryFolder outer(L"bw_tests_manifest");
    const std::filesystem::path game = outer.Path() / L"game";
    std::filesystem::create_directories(game / kDataFolder);
    WriteText(outer.Path() / L"victim.txt", "keep me");
    const std::string hash = Sha256Hex(Bytes("keep me"));
    WriteText(game / kManifestPath, "# Black & White decompilation 1.0.0\r\n" + hash + " R ..\\victim.txt\r\n" + hash +
                                        " R C:\\victim.txt\r\nnot a line\r\n");
    const std::optional<Manifest> manifest = ReadManifest(game);
    CHECK(manifest.has_value() && manifest->entries.empty());
    (void)RemoveFiles(game);
    CHECK_EQ(ReadText(outer.Path() / L"victim.txt"), std::string("keep me"));
}

TEST(TextConvertsBetweenUtf8AndUtf16)
{
    CHECK(bw::Widen("B\xc3\xa4r \xe7\x8c\xab") == L"B\u00e4r \u732b");
    CHECK_EQ(bw::Utf8(L"B\u00e4r \u732b"), std::string("B\xc3\xa4r \xe7\x8c\xab"));
    CHECK(bw::Widen("").empty());
}

TEST(SetupAsksForAdministratorRightsOnlyWhenNeeded)
{
    const TemporaryFolder folder(L"bw_tests_rights");
    CHECK(!NeedsElevation(folder.Path()));
    std::wstring system(MAX_PATH, wchar_t{});
    system.resize(GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size())));
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
                          GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size) &&
                          elevation.TokenIsElevated != 0;
    if (token != nullptr) {
        CloseHandle(token);
    }
    CHECK(NeedsElevation(system) == !elevated);  // an administrator may write to System32
}
