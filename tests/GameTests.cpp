// Tests for decompiled logic that does not need the game process.

#include "Test.h"

#include "core/Hook.h"
#include "fixes/Fixes.h"
#include "game/black/CPUCheck.h"
#include "game/lhlib/LHScreen.h"

#include <algorithm>
#include <limits>

using black::CpuSignature;
using black::CpuVendor;

namespace {

constexpr std::uint32_t Signature(std::uint32_t family, std::uint32_t model, std::uint32_t stepping = 0)
{
    return (family << 8) | (model << 4) | stepping;
}

/// The test the shipped executable performed at 0x0045c5e0.
constexpr bool OriginalEarlyPentiumTest(std::uint32_t eax)
{
    return (eax & 0x500) == 0x500;
}

const bw::HookDescriptor* FindHook(bw::Address address)
{
    const auto hooks = bw::HookRegistry::All();
    const auto hook = std::ranges::find(hooks, address, &bw::HookDescriptor::original);
    return hook != hooks.end() ? &*hook : nullptr;
}

}  // namespace

TEST(EarlyPentiumIsFamilyFive)
{
    CHECK(black::IsEarlyPentiumSignature(Signature(5, 2)));
    CHECK(!black::IsEarlyPentiumSignature(Signature(6, 0xA)));
    CHECK(!black::IsEarlyPentiumSignature(Signature(4, 8)));
}

TEST(EarlyPentiumNoLongerMatchesFamilyFifteen)
{
    // AMD Zen reports base family 15 with extended family; the original mask test misread it as a Pentium.
    constexpr std::uint32_t zen3 = 0x00A20F10;
    CHECK(OriginalEarlyPentiumTest(zen3));
    CHECK(!black::IsEarlyPentiumSignature(zen3));
    CHECK(OriginalEarlyPentiumTest(Signature(7, 0)));
    CHECK(!black::IsEarlyPentiumSignature(Signature(7, 0)));
}

TEST(CpuSignatureFields)
{
    constexpr CpuSignature signature{0x2000 | Signature(6, 8, 3)};
    CHECK_EQ(signature.Family(), 6u);
    CHECK_EQ(signature.Model(), 8u);
    CHECK_EQ(signature.Stepping(), 3u);
    CHECK_EQ(signature.Type(), 2u);
    CHECK(black::CpuTypeName(signature) == "Secondary Processor");
}

TEST(CpuVendorIds)
{
    CHECK(black::ParseCpuVendor("GenuineIntel") == CpuVendor::Intel);
    CHECK(black::ParseCpuVendor("AuthenticAMD") == CpuVendor::Amd);
    CHECK(black::ParseCpuVendor("CentaurHauls") == CpuVendor::Centaur);
    CHECK(black::ParseCpuVendor("UMC UMC UMC ") == CpuVendor::Umc);
    CHECK(black::ParseCpuVendor("HygonGenuine") == CpuVendor::Unknown);
}

TEST(CpuModelTable)
{
    CHECK(black::CpuModelName(CpuVendor::Intel, CpuSignature{Signature(6, 8)}) ==
          "Intel P3 (0.18 Micrometer process) with 256 K on-die L2 cache");
    CHECK(black::CpuModelName(CpuVendor::Intel, CpuSignature{Signature(6, 0xA)}) ==
          "Intel P3 (0.18 Micrometer process) with 1/2 Meg on-die L2 cache");
    CHECK(black::CpuModelName(CpuVendor::Amd, CpuSignature{Signature(6, 4)}) ==
          "AMD K7 (0.18 Micrometer process) with 256 K on-die L2 cache");
    CHECK(black::CpuModelName(CpuVendor::Intel, CpuSignature{Signature(5, 7)}) == "Intel P54C");
    // Family 15 is "P4" for every vendor, exactly as shipped.
    CHECK(black::CpuModelName(CpuVendor::Amd, CpuSignature{Signature(15, 1)}) == "Intel P4 or some variant");
    CHECK(black::CpuModelName(CpuVendor::Intel, CpuSignature{Signature(3, 0)}) == "Unknown processor type");
    // Not in the 2001 table: the caller substitutes the brand string.
    CHECK(black::CpuModelName(CpuVendor::Intel, CpuSignature{Signature(6, 0xE)}).empty());
}

TEST(VideoMemoryClamp)
{
    constexpr std::uint64_t mib = 1ull << 20;
    CHECK_EQ(lhlib::ClampVideoMemory(512 * mib, 1024), static_cast<std::int32_t>(512 * mib));
    CHECK_EQ(lhlib::ClampVideoMemory(24 * 1024 * mib, 1024), static_cast<std::int32_t>(1024 * mib));
    CHECK_EQ(lhlib::ClampVideoMemory(24 * 1024 * mib, 0), std::numeric_limits<std::int32_t>::max());
    // The start-up test (free < 13,000,000 means low detail) must see a large card as large.
    CHECK(lhlib::ClampVideoMemory(0xFFFF'F000ull, 0) >= 13'000'000);
}

TEST(PhysicalMemoryClamp)
{
    MEMORYSTATUS status{};
    status.dwTotalPhys = 0x7FFF'FFFF;
    status.dwAvailPhys = 0x6000'0000;
    bw::fixes::ClampMemoryStatus(status, 512);
    CHECK_EQ(status.dwTotalPhys, SIZE_T{512} << 20);
    CHECK_EQ(status.dwAvailPhys, SIZE_T{512} << 20);

    status.dwTotalPhys = 64u << 20;
    status.dwAvailPhys = 32u << 20;
    bw::fixes::ClampMemoryStatus(status, 512);
    CHECK_EQ(status.dwTotalPhys, SIZE_T{64} << 20);
    CHECK_EQ(status.dwAvailPhys, SIZE_T{32} << 20);
}

TEST(CPUCheckHooksMatchOriginalConventions)
{
    struct Expected {
        bw::Address address;
        bw::CallingConvention convention;
        std::uint16_t calleeStackBytes;
    };
    using enum bw::CallingConvention;
    constexpr Expected expected[] = {
        {0x0045c550, Thiscall, 0},  // constructor: ret
        {0x0045c580, Thiscall, 4},  // scalar deleting destructor: ret 4
        {0x0045c5a0, Thiscall, 0},  // destructor: ret
        {0x0045c5e0, Cdecl, 0},
        {0x0045c610, Thiscall, 0},
        {0x0045d260, Stdcall, 16},  // DirectDraw enumeration callback: ret 0x10
        {0x0045d4a0, Cdecl, 0},
        {0x00749a70, Thiscall, 0},  // LHScreen::GetAvailableVideoMemory: ret
    };
    for (const Expected& hook : expected) {
        const bw::HookDescriptor* registered = FindHook(hook.address);
        CHECK(registered != nullptr);
        if (registered != nullptr) {
            CHECK(registered->convention == hook.convention);
            CHECK_EQ(registered->calleeStackBytes, hook.calleeStackBytes);
        }
    }
}
