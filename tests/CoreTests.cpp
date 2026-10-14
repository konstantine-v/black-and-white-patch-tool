#include "Test.h"

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Image.h"
#include "core/Text.h"
#include "core/Windows.h"

#include <array>
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
