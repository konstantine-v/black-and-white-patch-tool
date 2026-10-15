#include "Test.h"

#include "core/Bitmap.h"
#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Handle.h"
#include "core/Hook.h"
#include "core/Image.h"
#include "core/Log.h"
#include "core/Module.h"
#include "core/Text.h"
#include "core/Windows.h"

#include <shellapi.h>

#include <array>
#include <filesystem>
#include <vector>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace {

int __cdecl CdeclAdd(int a, int b) noexcept
{
    return a + b;
}

int __stdcall StdcallMix(char, short, int, double) noexcept
{
    return 0;
}

}  // namespace

TEST(StackBytesRoundToSlots)
{
    using bw::detail::kStackBytes;
    CHECK_EQ(kStackBytes<>, 0);
    CHECK_EQ((kStackBytes<char, short, int>), 12);
    CHECK_EQ((kStackBytes<double, void*>), 12);
}

TEST(FunctionHookTraits)
{
    const bw::HookDescriptor cdeclHook = bw::detail::MakeFunctionHook(0x401000, &CdeclAdd, "CdeclAdd");
    CHECK(cdeclHook.convention == bw::CallingConvention::Cdecl);
    CHECK_EQ(cdeclHook.calleeStackBytes, 0);

    const bw::HookDescriptor stdcallHook = bw::detail::MakeFunctionHook(0x401000, &StdcallMix, "StdcallMix");
    CHECK(stdcallHook.convention == bw::CallingConvention::Stdcall);
    CHECK_EQ(stdcallHook.calleeStackBytes, 4 + 4 + 4 + 8);
}

TEST(TextHelpersTruncateAndTerminate)
{
    std::array<char, 8> buffer{};
    bw::text::CopyInto(buffer.data(), buffer.size(), "abc");
    CHECK(std::string_view(buffer.data()) == "abc");
    bw::text::AppendInto(buffer.data(), buffer.size(), "defghij");
    CHECK(std::string_view(buffer.data()) == "abcdefg");
    bw::text::FormatInto(buffer.data(), buffer.size(), "{}-{:x}", 12, 255);
    CHECK(std::string_view(buffer.data()) == "12-ff");
    bw::text::AppendFormatInto(buffer.data(), buffer.size(), "{}", 123456);
    CHECK(std::string_view(buffer.data()) == "12-ff12");
}

TEST(AddressListParsing)
{
    const auto addresses = bw::ParseAddressList(L"0x0045c610, 45d4a0\tzz 0X00749A70");
    CHECK_EQ(addresses.size(), 3u);
    if (addresses.size() == 3) {
        CHECK_EQ(addresses[0], 0x0045c610u);
        CHECK_EQ(addresses[1], 0x0045d4a0u);
        CHECK_EQ(addresses[2], 0x00749a70u);
    }
}

TEST(Sha256KnownVector)
{
    constexpr std::string_view input = "abc";
    const auto digest = bw::image::ComputeSha256(std::as_bytes(std::span(input.data(), input.size())));
    CHECK(digest.has_value());
    if (digest) {
        const auto hex = bw::image::ToHex(*digest);
        CHECK(std::string_view(hex.data(), hex.size()) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
}

TEST(RegisteredHooksAreUniqueAndInsideText)
{
    const auto hooks = bw::HookRegistry::All();
    CHECK(!hooks.empty());
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        CHECK(hooks[i].original >= 0x00401000 && hooks[i].original + 5 <= 0x007ea1cd);
        if (i + 1 < hooks.size()) {
            CHECK(hooks[i].original < hooks[i + 1].original);
        }
    }
}

TEST(DllStaysInertOutsideTheGame)
{
    // BWDecomp.dll is built next to bw_tests.exe. Outside runblack.exe its DllMain must succeed without patching.
    std::wstring dll(MAX_PATH, L'\0');
    dll.resize(GetModuleFileNameW(nullptr, dll.data(), static_cast<DWORD>(dll.size())));
    dll.replace(dll.find_last_of(L'\\') + 1, std::wstring::npos, L"BWDecomp.dll");

    HMODULE module = LoadLibraryW(dll.c_str());
    CHECK(module != nullptr);
    if (module != nullptr) {
        FreeLibrary(module);
    }
}

TEST(QuotedArgumentsReadBackUnchanged)
{
    const std::vector<std::wstring> arguments = {
        L"plain", L"with space", LR"(quote"inside)", LR"(trailing\)", LR"(back\\"slash)", LR"(C:\Games\Black & White\)", L"",
    };
    std::wstring commandLine = L"program.exe";
    for (const std::wstring& argument : arguments) {
        commandLine += L' ';
        commandLine += bw::QuoteArgument(argument);
    }
    int count = 0;
    LPWSTR* parsed = CommandLineToArgvW(commandLine.c_str(), &count);
    CHECK(parsed != nullptr);
    if (parsed == nullptr) {
        return;
    }
    CHECK_EQ(static_cast<std::size_t>(count), arguments.size() + 1);
    for (std::size_t i = 0; i < arguments.size() && i + 1 < static_cast<std::size_t>(count); ++i) {
        CHECK(arguments[i] == parsed[i + 1]);
    }
    LocalFree(parsed);
}

TEST(UniqueHandleClosesAndMoves)
{
    bw::UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    CHECK(static_cast<bool>(event));
    const HANDLE raw = event.Get();
    bw::UniqueHandle moved(std::move(event));
    CHECK(!event);
    CHECK(moved.Get() == raw);
    moved.Reset();
    DWORD flags = 0;
    CHECK(!GetHandleInformation(raw, &flags));  // closed
    CHECK(!bw::UniqueHandle(INVALID_HANDLE_VALUE));
}

TEST(ModulePathIsTheExecutable)
{
    const std::filesystem::path path = bw::ModulePath(nullptr);
    CHECK(path.filename() == L"bw_tests.exe");
    CHECK(std::filesystem::exists(path));
    CHECK(bw::ModuleDirectory(nullptr) == path.parent_path());
}

TEST(NtHeadersAreValidated)
{
    CHECK(bw::image::NtHeaders(GetModuleHandleW(nullptr)) != nullptr);
    CHECK(bw::image::NtHeaders(nullptr) == nullptr);
    alignas(8) std::array<std::byte, 256> notAnImage{};
    CHECK(bw::image::NtHeaders(reinterpret_cast<HMODULE>(notAnImage.data())) == nullptr);
    CHECK(bw::image::FindSection(GetModuleHandleW(nullptr), ".text").has_value());
}

TEST(PathTextIsUtf8)
{
    CHECK_EQ(bw::log::PathText(L"C:\\Games\\B\u00e4r\\\u732b.png"), std::string("C:\\Games\\B\xc3\xa4r\\\xe7\x8c\xab.png"));
    CHECK_EQ(bw::log::PathText({}), std::string());
}

TEST(BitmapsRoundTripThroughPng)
{
    const std::filesystem::path file = std::filesystem::temp_directory_path() / L"bw_tests_bitmap.png";
    bw::Bitmap bitmap;
    bitmap.width = 3;
    bitmap.height = 2;
    bitmap.argb = {0xffff0000u, 0x8000ff00u, 0x000000ffu, 0xffffffffu, 0x40102030u, 0xff000000u};
    CHECK(bw::WritePng(file, bitmap));
    bw::Bitmap read;
    CHECK(bw::ReadBitmap(file, read));
    CHECK_EQ(read.width, 3u);
    CHECK_EQ(read.height, 2u);
    CHECK(read.argb == bitmap.argb);
    std::error_code error;
    std::filesystem::remove(file, error);

    bw::Bitmap wrongSize;
    wrongSize.width = 4;
    wrongSize.height = 4;
    wrongSize.argb.resize(3);
    CHECK(!bw::WritePng(file, wrongSize));
    CHECK(!bw::ReadBitmap(std::filesystem::temp_directory_path() / L"bw_tests_missing.png", read));
}
