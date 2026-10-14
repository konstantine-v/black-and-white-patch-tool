#include "game/lh3dlib/LH3DRender.h"

#include "core/Call.h"
#include "core/Hook.h"
#include "core/Log.h"
#include "game/lhlib/LHScreen.h"

namespace lh3dlib {

namespace {

// LH3D's debug printf (OutputDebugString); kept so the game's own trace stays identical.
inline constexpr bw::CdeclFunction<0x00776a70, void(const char*, ...)> Trace{};

// Not decompiled yet.
inline constexpr bw::CdeclFunction<0x00795e40, void()> ResetTextureManager{};
inline constexpr bw::CdeclFunction<0x00785fd0, std::int32_t()> OpenDirect3D{};
inline constexpr bw::FastcallFunction<0x00773ee0, void(std::uint32_t width, std::uint32_t height)> SetViewport{};
inline constexpr bw::CdeclFunction<0x0078d2a0, void()> InitialiseMaterials{};
inline constexpr bw::ThiscallFunction<0x00521920, void(float*)> NormaliseVector{};
inline constexpr bw::CdeclFunction<0x0078eda0, void()> Initialise0078eda0{};
inline constexpr bw::CdeclFunction<0x00764950, void()> Initialise00764950{};
inline constexpr bw::CdeclFunction<0x00767df0, void()> Initialise00767df0{};
inline constexpr bw::FastcallFunction<0x007cb440, void(std::uint32_t height)> Initialise007cb440{};
inline constexpr bw::CdeclFunction<0x007c3a10, void()> Initialise007c3a10{};
inline constexpr bw::CdeclFunction<0x00779210, void()> Initialise00779210{};
inline constexpr bw::CdeclFunction<0x007cd000, void(std::int32_t, std::int32_t)> Initialise007cd000{};
inline constexpr bw::CdeclFunction<0x00778d00, void()> Initialise00778d00{};
inline constexpr bw::CdeclFunction<0x00779ff0, void()> Initialise00779ff0{};

// Unnamed globals Start resets.
BW_GLOBAL(std::int32_t, g_unknown00de949c, 0x00de949c);
BW_GLOBAL(std::int32_t, g_unknown00de94a0, 0x00de94a0);
BW_GLOBAL(std::int32_t, g_unknown00de94b0, 0x00de94b0);
BW_GLOBAL(std::int32_t, g_unknown00de94c0, 0x00de94c0);
BW_GLOBAL(std::int32_t, g_unknown00de9484, 0x00de9484);
BW_GLOBAL(bw::Address, g_unknown00de9474, 0x00de9474);
BW_GLOBAL(float, g_vector00dc8cd0, 0x00dc8cd0);  // three floats, normalised in place

constexpr std::int32_t kFullCacheSize = 256;
constexpr std::int32_t kLowMemoryCacheSize = 128;
constexpr bw::Address kTable00b5a7b0 = 0x00b5a7b0;

}  // namespace

std::int32_t LH3DRender::Start() noexcept
{
    Trace("#OPEN#\n");
    if (g_renderEnabled == 0) {
        return 0;
    }
    if (g_renderOpen != 0) {
        Trace("<<<<< LH3DRender Start: multi calls. >>>>>\n");
        return 0;
    }

    Trace("%d TEXTURE AVAILABLE \n", lhlib::g_screen.GetAvailableVideoMemory());
    if (g_renderStatistics.initialised == 0) {
        g_renderStatistics = RenderStatistics{{0, 0, 0, 0}, 1};
    }
    else {
        Trace("Already Open!\n");
    }

    ResetTextureManager();
    g_renderOpen = 1;
    g_unknown00de949c = 0;
    g_unknown00de94a0 = 0;
    OpenDirect3D();
    if (g_device == nullptr) {
        // Original: continued into SetViewport and crashed on the null device (0x00775031).
        LogDeviceFailure();
        ReleaseFailedStart();
        g_renderOpen = 0;
        return 0;
    }

    SetViewport(lhlib::g_screen.Width(), lhlib::g_screen.Height());
    InitialiseMaterials();
    float* vector = &g_vector00dc8cd0;
    vector[0] = 1.0f;
    vector[1] = 0.0f;
    vector[2] = 2.0f;
    NormaliseVector(vector);

    const std::int32_t cacheSize = g_lowVideoMemory != 0 ? kLowMemoryCacheSize : kFullCacheSize;
    g_renderCacheSize = cacheSize;
    RenderCacheEntry* cache = &g_renderCache;
    for (std::int32_t i = 0; i < cacheSize; ++i) {
        cache[i].inUse = 0;
    }
    g_renderCacheLast = static_cast<float>(cacheSize - 1);
    g_unknown00de94b0 = 0;
    g_unknown00de9474 = kTable00b5a7b0;

    Initialise0078eda0();
    Initialise00764950();
    Initialise00767df0();
    Initialise007cb440(lhlib::g_screen.Height());
    Initialise007c3a10();
    Initialise00779210();
    Initialise007cd000(0, 12);
    Initialise00778d00();
    g_unknown00de94c0 = 0;
    g_unknown00de9484 = 0;
    Initialise00779ff0();

    Trace(g_zBuffer16 != 0 ? "using z buffer for sun before check\n" : "no z buffer compatibility before check\n");
    return 1;
}

void LH3DRender::LogDeviceFailure() noexcept
{
    HRESULT result = E_POINTER;
    IDirectDrawSurface* back = lhlib::g_screen.BackSurface();
    if (g_direct3D != nullptr && back != nullptr) {
        IDirectDrawSurface7* back7 = nullptr;
        result = back->QueryInterface(IID_IDirectDrawSurface7, reinterpret_cast<void**>(&back7));
        if (SUCCEEDED(result)) {
            IDirect3DDevice7* device = nullptr;
            result = g_direct3D->CreateDevice(IID_IDirect3DHALDevice, back7, &device);
            if (device != nullptr) {
                device->Release();
            }
            back7->Release();
        }
    }
    bw::log::Error("LH3DRender: no Direct3D device for {}x{}x{} (CreateDevice 0x{:08x})", lhlib::g_screen.Width(),
                   lhlib::g_screen.Height(), lhlib::g_screen.BitsPerPixel(), static_cast<std::uint32_t>(result));
}

void LH3DRender::ReleaseFailedStart() noexcept
{
    auto release = [](auto*& object) {
        if (object != nullptr) {
            object->Release();
            object = nullptr;
        }
    };
    release(g_zBuffer);
    release(g_direct3D);
}

BW_HOOK_FUNCTION(0x00784520, LH3DRender::Start);

}  // namespace lh3dlib
