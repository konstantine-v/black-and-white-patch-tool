// Tests for decompiled logic that does not need the game process.

#include "Test.h"

#include "core/Hook.h"
#include "diagnostics/DirectXTrace.h"
#include "diagnostics/Screenshot.h"
#include "fixes/Fixes.h"
#include "game/black/CPUCheck.h"
#include "game/black/PCMain.h"
#include "game/lhlib/LHScreen.h"

#include <algorithm>
#include <memory>
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

TEST(HooksMatchOriginalConventions)
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
        {0x005f1250, Cdecl, 0},     // FindGameDisc
        {0x00748d70, Thiscall, 12}, // LHScreen::SetMode: ret 0xc
        {0x00784520, Cdecl, 0},     // LH3DRender::Start
        {0x00749a70, Thiscall, 0},  // LHScreen::GetAvailableVideoMemory: ret
        {0x007496c0, Thiscall, 4},  // LHScreen::Flip: ret 4
        {0x007484a0, Thiscall, 4},  // LHScreen::SetFullScreen: ret 4
        {0x00746e60, Thiscall, 0},  // LHWin::RequestQuit: ret
        {0x00746f00, Cdecl, 0},     // PumpMessages
        {0x00746fc0, Cdecl, 0},     // RegisterWindowClass
        {0x00747ee0, Cdecl, 0},     // ResizeWindowToMode
        {0x00746eb0, Cdecl, 0},     // ShowWindowMenu
        {0x00747470, Cdecl, 0},     // GetCursorFramePosition
        {0x0074c0f0, Thiscall, 4},  // LHMouse::SetPosition: ret 4
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

TEST(ColorDepthPromotion)
{
    CHECK_EQ(lhlib::EffectiveColorDepth(16, true), 32);
    CHECK_EQ(lhlib::EffectiveColorDepth(16, false), 16);
    CHECK_EQ(lhlib::EffectiveColorDepth(32, true), 32);
    CHECK_EQ(lhlib::EffectiveColorDepth(24, true), 24);
}

TEST(GameDiscIdentification)
{
    CHECK(black::IsGameDisc("BNW", "CDFS"));
    CHECK(!black::IsGameDisc("BNW", "NTFS"));
    CHECK(!black::IsGameDisc("BW2", "CDFS"));
}

TEST(FallbackModesPreferSameAspectThenSize)
{
    using lhlib::DisplayMode;
    const DisplayMode available[] = {{640, 480}, {1920, 1080}, {1920, 1440}, {2560, 1440}, {1600, 900}, {3413, 1920}};
    const auto ranked = lhlib::RankFallbackModes(available, {2560, 1440});
    CHECK_EQ(ranked.size(), 4u);
    if (ranked.size() == 4) {
        CHECK(ranked[0] == (DisplayMode{1920, 1080}));  // 16:9, largest
        CHECK(ranked[1] == (DisplayMode{1600, 900}));
        CHECK(ranked[2] == (DisplayMode{1920, 1440}));  // 4:3, larger than 640x480
        CHECK(ranked[3] == (DisplayMode{640, 480}));
    }
}

TEST(ScreenshotChannelsScaleToEightBits)
{
    using bw::diagnostics::ExtractChannel;
    CHECK_EQ(ExtractChannel(0x00ff8040, 0x00ff0000), 0xffu);
    CHECK_EQ(ExtractChannel(0x00ff8040, 0x0000ff00), 0x80u);
    CHECK_EQ(ExtractChannel(0x00ff8040, 0x000000ff), 0x40u);
    // RGB 565: full red, half green, no blue.
    CHECK_EQ(ExtractChannel(0xf800 | (31 << 5), 0xf800), 255u);
    CHECK_EQ(ExtractChannel(0xf800 | (31 << 5), 0x07e0), 125u);  // 31/63 * 255 = 125.5
    CHECK_EQ(ExtractChannel(0xf800 | (31 << 5), 0x001f), 0u);
}

TEST(ScreenshotChecksums)
{
    const std::uint8_t iend[] = {'I', 'E', 'N', 'D'};
    CHECK_EQ(bw::diagnostics::Crc32(iend), 0xae426082u);
    const std::uint8_t wikipedia[] = {'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'};
    CHECK_EQ(bw::diagnostics::Adler32(wikipedia), 0x11e60398u);
}

TEST(ScreenshotPngLayout)
{
    const std::uint8_t rgb[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    const std::vector<std::uint8_t> png = bw::diagnostics::EncodePng(2, 2, rgb);
    const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    CHECK(std::equal(std::begin(signature), std::end(signature), png.begin()));
    // IHDR: length 13, width 2 and height 2 big-endian, 8 bits per channel, RGB.
    CHECK_EQ(png[11], 13u);
    CHECK_EQ(png[19], 2u);
    CHECK_EQ(png[23], 2u);
    CHECK_EQ(png[24], 8u);
    CHECK_EQ(png[25], 2u);
    // IDAT holds zlib header (2) + one stored block (5 + two rows of 7) + Adler-32 (4) = 25 bytes.
    CHECK_EQ(png[33], 0u);
    CHECK_EQ(png[36], 25u);
    // Ends with the IEND chunk.
    const std::uint8_t end[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82};
    CHECK(std::equal(std::begin(end), std::end(end), png.end() - 12));
}

TEST(ShutdownWatchdogEndsAStalledProcess)
{
    std::wstring self(MAX_PATH, L'\0');
    self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    std::wstring commandLine = L"\"" + self + L"\" --stalled-shutdown";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(self.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        nullptr, &startup, &process) != FALSE;
    CHECK(started);
    if (!started) {
        return;
    }
    const DWORD waited = WaitForSingleObject(process.hProcess, 15'000);
    CHECK_EQ(waited, static_cast<DWORD>(WAIT_OBJECT_0));
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 0);
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CHECK_EQ(exitCode, bw::fixes::kShutdownWatchdogExitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

TEST(FrameFitsWindowKeepingAspectRatio)
{
    using lhlib::FitFrame;
    // 1920x1080 in a 2560x1440 window: exact 4/3 scale, no bars.
    RECT r = FitFrame({1920, 1080}, {2560, 1440}, true);
    CHECK(r.left == 0 && r.top == 0 && r.right == 2560 && r.bottom == 1440);
    // 4:3 in 16:9: pillarbox.
    r = FitFrame({1600, 1200}, {2560, 1440}, true);
    CHECK(r.left == 320 && r.top == 0 && r.right == 2240 && r.bottom == 1440);
    // 16:9 in 16:10: letterbox.
    r = FitFrame({1920, 1080}, {1920, 1200}, true);
    CHECK(r.left == 0 && r.top == 60 && r.right == 1920 && r.bottom == 1140);
    // Stretching ignores the aspect ratio.
    r = FitFrame({1600, 1200}, {2560, 1440}, false);
    CHECK(r.left == 0 && r.top == 0 && r.right == 2560 && r.bottom == 1440);
}

TEST(FrameMappingRoundTrips)
{
    const lhlib::DisplayMode frame{1920, 1080};
    const RECT rect = lhlib::FitFrame(frame, {2560, 1440}, true);
    for (const POINT p : {POINT{0, 0}, POINT{1919, 1079}, POINT{960, 540}, POINT{17, 1033}}) {
        const POINT client = lhlib::MapFromFrame(p, rect, frame);
        const POINT back = lhlib::MapToFrame(client, rect, frame);
        CHECK(back.x == p.x && back.y == p.y);
    }
    // Points in the bars or outside the window clamp to the frame's edge.
    const RECT pillar = lhlib::FitFrame({1600, 1200}, {2560, 1440}, true);
    POINT p = lhlib::MapToFrame({10, -5}, pillar, {1600, 1200});
    CHECK(p.x == 0 && p.y == 0);
    p = lhlib::MapToFrame({2550, 2000}, pillar, {1600, 1200});
    CHECK(p.x == 1599 && p.y == 1199);
}

TEST(CallCounterCountsPerMethodAndCaller)
{
    auto counter = std::make_unique<bw::diagnostics::CallCounter>();
    for (int i = 0; i < 1000; ++i) {
        counter->Add(0x0d14, 0x00784600);  // IDirect3DDevice7::SetRenderState from one caller
    }
    counter->Add(0x0d14, 0x00784700);
    counter->Add(0x0d23, 0x00784600);
    std::uint32_t total = 0;
    std::uint32_t entries = 0;
    counter->ForEach([&](const bw::diagnostics::CallCounter::Entry& entry) {
        ++entries;
        total += entry.count;
        if (entry.method == 0x0d14 && entry.caller == 0x00784600) {
            CHECK_EQ(entry.count, 1000u);
        }
    });
    CHECK_EQ(entries, 3u);
    CHECK_EQ(total, 1002u);
    CHECK_EQ(counter->Dropped(), 0u);
}
