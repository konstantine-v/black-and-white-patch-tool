#pragma once

#include "core/Windows.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

/// Inventory of the game's DirectDraw / Direct3D 7 usage (TraceDirectX): every interface method the game calls,
/// from which original function, how often. The groundwork for the Direct3D 9 renderer, which has to provide
/// exactly these calls.
///
/// When the game creates its DirectDraw object, the method tables of every DirectDraw/Direct3D 7 interface in
/// ddraw.dll are patched so that each method first counts (method, return address) and then continues in ddraw.dll.
/// The counts are written to BWDecomp.directx.csv when the process ends; tools/directx_report.py summarises them.
namespace bw::diagnostics {

void InstallDirectXTrace(HMODULE game, const std::filesystem::path& output) noexcept;

/// Instruments the method table of a Direct3D device the game created, if it is not yet instrumented: ddraw.dll
/// has several device implementations, so the probe device created at start-up need not share the game's table.
void TraceDirect3DDevice(void* device) noexcept;

/// Re-applies the counting stubs where the runtime replaced them. Direct3D 7 rewrites a device's draw entries
/// (DrawPrimitive, DrawIndexedPrimitive and their vertex-buffer forms) as rendering state changes; called once per
/// frame by LHScreen::Flip.
void RefreshDirectXTrace() noexcept;

/// Writes the counts collected so far (also called when the process ends).
void WriteDirectXTrace() noexcept;

/// A fixed-capacity, lock-free counter of (method, caller) pairs; exposed for tests.
class CallCounter {
public:
    struct Entry {
        std::uint32_t method;
        std::uint32_t caller;
        std::uint32_t count;
    };

    void Add(std::uint32_t method, std::uint32_t caller) noexcept;
    /// Calls `visit(Entry)` for every recorded pair.
    template <typename Visit>
    void ForEach(Visit visit) const;
    [[nodiscard]] std::uint32_t Dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

private:
    static constexpr std::size_t kCapacity = std::size_t{1} << 15;
    struct Slot {
        std::atomic<std::uint64_t> key{0};
        std::atomic<std::uint32_t> count{0};
    };
    Slot m_slots[kCapacity];
    std::atomic<std::uint32_t> m_dropped{0};
};

template <typename Visit>
void CallCounter::ForEach(Visit visit) const
{
    for (const Slot& slot : m_slots) {
        const std::uint64_t key = slot.key.load(std::memory_order_acquire);
        if (key != 0) {
            visit(Entry{static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key),
                        slot.count.load(std::memory_order_relaxed)});
        }
    }
}

}  // namespace bw::diagnostics
