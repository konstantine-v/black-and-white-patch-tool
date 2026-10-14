#include "game/lhlib/LHScreen.h"

#include "core/Config.h"
#include "core/Hook.h"
#include "core/Log.h"

#include <ddraw.h>

#include <algorithm>
#include <limits>

namespace lhlib {

std::int32_t ClampVideoMemory(std::uint64_t bytes, std::uint32_t limitMiB) noexcept
{
    std::uint64_t limit = std::numeric_limits<std::int32_t>::max();
    if (limitMiB != 0) {
        limit = std::min<std::uint64_t>(limit, std::uint64_t{limitMiB} << 20);
    }
    return static_cast<std::int32_t>(std::min(bytes, limit));
}

std::int32_t LHScreen::GetAvailableVideoMemory() noexcept
{
    // Original: QueryInterface for IDirectDraw2 (never released), GetAvailableVidMem(DDSCAPS_VIDEOMEMORY) and return
    // the free byte count as a signed int, uninitialised if the call failed. On current hardware the count wraps
    // negative (or is garbage), so PCMain switched every large card to low-detail textures.
    if (m_directDraw == nullptr) {
        return 0;
    }
    IDirectDraw2* directDraw2 = nullptr;
    if (FAILED(m_directDraw->QueryInterface(IID_IDirectDraw2, reinterpret_cast<void**>(&directDraw2)))) {
        bw::log::Warning("LHScreen: IDirectDraw2 unavailable; reporting no video memory");
        return 0;
    }
    DDSCAPS caps{};
    caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    DWORD total = 0;
    DWORD free = 0;
    const HRESULT result = directDraw2->GetAvailableVidMem(&caps, &total, &free);
    directDraw2->Release();
    if (FAILED(result)) {
        bw::log::Warning("LHScreen: GetAvailableVidMem failed (0x{:08x})", static_cast<std::uint32_t>(result));
        return 0;
    }
    return ClampVideoMemory(free, bw::GetConfig().maxReportedVideoMemoryMiB);
}

BW_HOOK_METHOD(0x00749a70, LHScreen, GetAvailableVideoMemory);

}  // namespace lhlib
